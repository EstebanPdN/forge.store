#!/usr/bin/env python3
"""Production Forge Mode source, independent synthetic crypto vectors; no personal data."""
import pathlib,subprocess,tempfile
root=pathlib.Path(__file__).resolve().parents[1]
vector=r'''
const fs=require('fs'),c=require('crypto');
const salt=Buffer.alloc(16,17),iv=Buffer.alloc(12,23),key=c.pbkdf2Sync('host-fixture-only',salt,10000,32,'sha256');
const file={type:'downloadUrl',personal:true,name:'fixture.bin',url:'https://example.invalid/fixture.bin',output:'/3ds/demo/fixture.bin',size:32,sha256:'ab'.repeat(32)};
const zipMember={offset:100,bytes:170000000,key:'cd'.repeat(32)};
const data={...file,size:200000000,zipMember};
const download={type:'downloadUrl',name:'fixture.cia',url:file.url,output:'/3ds/forge-store/tmp/forge-preview.cia',size:7000,sha256:file.sha256,zipMember:{...zipMember,bytes:5000}};
const build={tag:'Preview 1',forgeTitleId:'0004000012345600',o3ds:false,download};
const plain=JSON.stringify({demo:[file],preview:{data:[data],build}});
const aes=c.createCipheriv('aes-256-gcm',key,iv);aes.setAAD(Buffer.from('forge.store/private-assets/v1'));
const sealed=Buffer.concat([aes.update(plain),aes.final()]),head=Buffer.alloc(56);head.write('FM01');head.writeUInt32LE(10000,4);salt.copy(head,8);iv.copy(head,24);aes.getAuthTag().copy(head,36);head.writeUInt32LE(sealed.length,52);
fs.writeFileSync('romfs:/personal-assets.fme',Buffer.concat([head,sealed]));
'''
with tempfile.TemporaryDirectory(prefix='forge-mode-test-') as temp:
 p=pathlib.Path(temp);(p/'romfs:').mkdir();(p/'sdmc:/3ds/forge-store').mkdir(parents=True)
 subprocess.run(['node','-e',vector],cwd=p,check=True)
 cmd=['clang','-std=gnu11','-g','-O1','-Wall','-Wextra','-fsanitize=address,undefined','-I/opt/homebrew/include',str(root/'tests/forge-mode.c'),'-L/opt/homebrew/lib','-ljansson','-lcrypto','-o',str(p/'test')]
 subprocess.run(cmd,check=True);subprocess.run([str(p/'test')],cwd=p,check=True)
