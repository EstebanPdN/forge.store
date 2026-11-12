// Snapshot of the same source used by the web prototype. Run explicitly to refresh.
import {loadUniversal} from '../../src/js/core/universal.js';
import {mkdir,writeFile,readFile} from 'node:fs/promises';
const root=new URL('../',import.meta.url);
const artOnly=process.argv.includes('--art-only');
const apps=artOnly?JSON.parse(await readFile(new URL('romfs/universal.json',root),'utf8')).apps:await loadUniversal();
if(!artOnly){
const store=await (await fetch('https://db.universal-team.net/unistore/universal-db.unistore')).json();
const supported=new Set(['downloadFile','downloadRelease','extractFile','installCia','deleteFile','mkdir','move','rmdir']);
for(const a of apps){
 const entry=store.storeContent.find(e=>e.info.title===a.title);
 for(const [format,steps] of Object.entries(a.install)){
  const pair=Object.entries(entry||{}).find(([k])=>new RegExp('\\.'+format+'$','i').test(k)&&!/nightly|\[git\]|\[prerelease\]/i.test(k));
  const raw=pair&&(Array.isArray(pair[1])?pair[1]:pair[1].script);
  if(raw?.some(s=>!supported.has(s.type))){a.nativeUnsupported=true;continue;}
  let di=0,ei=0;
  for(const s of raw||[]){
   if(s.type==='downloadFile'||s.type==='downloadRelease'){
    const dst=steps.filter(x=>x.type==='downloadUrl')[di++];if(!dst)continue;
    if(s.type==='downloadRelease'){dst.repo=s.repo;dst.match=s.file;dst.url='';}
   }
   if(s.type==='extractFile'){const dst=steps.filter(x=>x.type==='extractFile')[ei++];if(dst&&s.input)dst.input=s.input;}
  }
 }
}
await writeFile(new URL('romfs/universal.json',root),JSON.stringify({updated:new Date().toISOString(),apps}));
}
await mkdir(new URL('build/universal-icons/',root),{recursive:true});
await mkdir(new URL('build/universal-banners/',root),{recursive:true});
let downloaded=0;
for(let start=0;start<apps.length;start+=8){await Promise.allSettled(apps.slice(start,start+8).map(async(a,k)=>{
 for(const [kind,url] of [['icons',a.icon],['banners',a.banner?.bg]]){
  if(typeof url!=='string')continue;
  try {const r=await fetch(url,{signal:AbortSignal.timeout(12000)});if(!r.ok)continue;
   await writeFile(new URL(`build/universal-${kind}/${start+k}.img`,root),Buffer.from(await r.arrayBuffer()));downloaded++;
  }catch(e){console.error(`${kind}: ${a.id}: ${e.message}`);}
 }
}));}
console.log({apps:apps.length,icons:downloaded,unsupported:apps.filter(a=>a.nativeUnsupported).length});
