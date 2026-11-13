#!/usr/bin/env python3
"""Read Forge CANM with independent SPICA code and reproduce the rejected W-slot defect.

Optional research harness; .NET and an external SPICA checkout are not native build
requirements. Generated files remain in 3ds/build/home-banner/reference-reader/.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
from xml.sax.saxutils import escape

ROOT=Path(__file__).resolve().parents[2]
PROGRAM=r'''
using System;
using System.IO;
using System.Text;
using SPICA.Formats.CtrGfx;
using SPICA.Formats.CtrGfx.Animation;
using SPICA.Serialization;
class Program {
    static int Main(string[] args) {
        try {
            using var fs=File.OpenRead(args[0]);
            var d=new BinaryDeserializer(fs,new SerializationOptions(LengthPos.BeforePtr,PointerType.SelfRelative));
            d.Deserialize<GfxHeader>();
            var raw=File.ReadAllBytes(args[0]);
            int offset=Encoding.ASCII.GetString(raw).IndexOf("CANM");
            if(offset<0)throw new Exception("Missing CANM");
            fs.Position=offset;
            var a=d.Deserialize<GfxAnimation>();
            if(a.Name!="COMMON" || a.TargetAnimGroupName!="SkeletalAnimation" || a.FramesCount!=312)
                throw new Exception("Unexpected animation identity/duration");
            int members=0;
            foreach(var e in a.Elements) {
                members++;
                if(e.Name!="Medallion" || e.Content is not GfxAnimTransform t)
                    throw new Exception("Unexpected rigid member");
                var groups=new[]{t.ScaleX,t.ScaleY,t.ScaleZ,t.RotationX,t.RotationY,t.RotationZ,t.TranslationX,t.TranslationY,t.TranslationZ};
                for(int slot=0;slot<groups.Length;slot++) {
                    var g=groups[slot];
                    if(g.KeyFrames.Count==0)throw new Exception("Missing channel "+slot);
                    foreach(var k in g.KeyFrames) {
                        if(!float.IsFinite(k.Value) || !float.IsFinite(k.Frame))throw new Exception("Nonfinite channel");
                        if(slot<3 && k.Value<=0)throw new Exception("Invalid scale");
                    }
                }
                var yaw=t.RotationY.KeyFrames;
                if(yaw.Count!=313 || yaw[0].Frame!=0 || Math.Abs(yaw[^1].Frame-312)>.001f)
                    throw new Exception("Lost yaw keys or frame range");
                if(Math.Abs(yaw[0].Value-yaw[^1].Value)>1e-6f)throw new Exception("Loop mismatch");
                float lo=float.MaxValue, hi=float.MinValue;
                foreach(var k in yaw) {lo=Math.Min(lo,k.Value); hi=Math.Max(hi,k.Value);}
                if(Math.Abs(lo+Math.PI*24/180)>1e-5 || Math.Abs(hi-Math.PI*24/180)>1e-5)
                    throw new Exception("Lost yaw amplitude");
                var expected=new[]{-8.61f,1f,.735f};
                for(int slot=6;slot<9;slot++)foreach(var k in groups[slot].KeyFrames)
                    if(Math.Abs(k.Value-expected[slot-6])>1e-5)throw new Exception("Moving pivot");
                Console.WriteLine("PASS: SPICA read all nine TRS channels, 313 yaw keys, closed 312-frame loop and fixed pivot");
            }
            if(members!=1)throw new Exception("Unexpected member count");
            return 0;
        } catch(Exception e) {Console.WriteLine(e.GetType().Name+": "+e.Message);return 1;}
    }
}
'''

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--spica',type=Path,required=True,help='External gdkchan/SPICA checkout root')
    ap.add_argument('--dotnet',default=shutil.which('dotnet'))
    ap.add_argument('--model',type=Path,default=ROOT/'3ds/meta/banner.cgfx')
    a=ap.parse_args()
    if not a.dotnet:ap.error('.NET SDK required for this optional reference harness')
    spica=a.spica.resolve()
    assert (spica/'SPICA/Formats/CtrGfx/Animation/GfxAnimTransform.cs').is_file()
    sdk=subprocess.check_output([a.dotnet,'--version'],text=True).strip()
    major=int(sdk.split('.')[0]);assert major>=8
    out=ROOT/'3ds/build/home-banner/reference-reader';out.mkdir(parents=True,exist_ok=True)
    source=escape(str(spica/'SPICA'))
    project=f'''<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup>
<OutputType>Exe</OutputType><TargetFramework>net{major}.0</TargetFramework>
<GenerateAssemblyInfo>false</GenerateAssemblyInfo><EnableDefaultCompileItems>false</EnableDefaultCompileItems>
<NoWarn>CA1416;CS0649;CS0169;CS0067;CS0414;SYSLIB0021</NoWarn>
</PropertyGroup><ItemGroup><Compile Include="Program.cs"/>
<Compile Include="{source}/**/*.cs" Exclude="{source}/obj/**/*.cs;{source}/bin/**/*.cs"/>
<PackageReference Include="System.Drawing.Common" Version="8.0.10"/></ItemGroup></Project>'''
    (out/'Reference.csproj').write_text(project);(out/'Program.cs').write_text(PROGRAM)
    subprocess.run([a.dotnet,'build',str(out/'Reference.csproj'),'--nologo','-v:q'],check=True,timeout=60)
    good=a.model.read_bytes();p=good.find(b'CANM');assert p>=0
    ptr=lambda q:q+struct.unpack_from('<i',good,q)[0]
    member=ptr(ptr(p+28)+40)
    flags=struct.unpack_from('<I',good,member)[0];assert flags&0x400000
    bad=bytearray(good);struct.pack_into('<I',bad,member,flags&~0x400000)
    rejected=out/'missing-rotation-w-flag.cgfx';rejected.write_bytes(bad)
    dll=out/f'bin/Debug/net{major}.0/Reference.dll'
    results=[]
    for path,expected in ((a.model,0),(rejected,1)):
        r=subprocess.run([a.dotnet,str(dll),str(path.resolve())],capture_output=True,text=True,timeout=10)
        assert r.returncode==expected,(path,r.stdout,r.stderr)
        if expected:assert 'EndOfStreamException' in r.stdout,r.stdout
        results.append(dict(case='missing_w_flag' if expected else 'corrected',exit=r.returncode,output=r.stdout.strip()))
        print(r.stdout.strip())
    report=dict(spica_commit=subprocess.check_output(['git','-C',str(spica),'rev-parse','HEAD'],text=True).strip(),dotnet_sdk=sdk,model_sha256=hashlib.sha256(good).hexdigest(),cases=results,hardware_tested=False)
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')

if __name__=='__main__':main()
