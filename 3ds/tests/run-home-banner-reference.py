#!/usr/bin/env python3
"""Read the complete fresh scene with external unmodified SPICA sources."""
import argparse,hashlib,json,shutil,subprocess
from pathlib import Path
from xml.sax.saxutils import escape
R=Path(__file__).resolve().parents[2]
def main():
 p=argparse.ArgumentParser();p.add_argument('--spica',type=Path,required=True);p.add_argument('--starfox-model',type=Path,required=True);p.add_argument('--model',type=Path,default=R/'3ds/meta/banner.cgfx');p.add_argument('--background-scale',type=float,default=1.);p.add_argument('--reader',type=Path,default=R/'3ds/tests/home-banner-fresh-reference.cs');p.add_argument('--dotnet',default=shutil.which('dotnet'));a=p.parse_args();assert a.dotnet
 major=int(subprocess.check_output([a.dotnet,'--version'],text=True).split('.')[0]);out=R/'3ds/build/home-banner'/a.reader.stem;out.mkdir(parents=True,exist_ok=True);source=escape(str(a.spica.resolve()/'SPICA'))
 (out/'Reader.csproj').write_text(f'''<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net{major}.0</TargetFramework><GenerateAssemblyInfo>false</GenerateAssemblyInfo><EnableDefaultCompileItems>false</EnableDefaultCompileItems><NoWarn>CA1416;CS0649;CS0169;CS0067;CS0414;SYSLIB0021</NoWarn></PropertyGroup><ItemGroup><Compile Include="Program.cs"/><Compile Include="{source}/**/*.cs" Exclude="{source}/obj/**/*.cs;{source}/bin/**/*.cs"/><PackageReference Include="System.Drawing.Common" Version="8.0.10"/></ItemGroup></Project>''')
 shutil.copy2(a.reader,out/'Program.cs');subprocess.run([a.dotnet,'build',str(out/'Reader.csproj'),'--nologo','-v:q'],check=True,timeout=60)
 r=subprocess.run([a.dotnet,str(out/f'bin/Debug/net{major}.0/Reader.dll'),str(a.model.resolve()),str(a.starfox_model.resolve()),str(a.background_scale)],capture_output=True,text=True,timeout=15);assert r.returncode==0,r.stdout+r.stderr;print(r.stdout)
 report=dict(model_sha256=hashlib.sha256(a.model.read_bytes()).hexdigest(),reference_sha256=hashlib.sha256(a.starfox_model.read_bytes()).hexdigest(),output=r.stdout.strip(),hardware_tested=False);(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
if __name__=='__main__':main()
