// Offline oracle: execute the actual web converter, add the existing native
// install-contract adaptations, and deep-compare every field to the C output.
// Usage: node 3ds/tools/check-universal.mjs full.json store.unistore native.json
import {readFile,writeFile} from 'node:fs/promises';
import assert from 'node:assert/strict';
const [fullPath,storePath,nativePath]=process.argv.slice(2);
if(!nativePath)throw Error('Expected full.json, .unistore and C-converted JSON paths');
const full=JSON.parse(await readFile(fullPath)),store=JSON.parse(await readFile(storePath));
globalThis.fetch=async url=>new Response(JSON.stringify(String(url).endsWith('full.json')?full:store));
const {loadUniversal}=await import('../../src/js/core/universal.js');
const expected=await loadUniversal();
const supported=new Set(['downloadFile','downloadRelease','extractFile','installCia','deleteFile','mkdir','move','rmdir']);
// Native detection data, as 3ds/source/universal_convert.c detection() writes it.
const stamp=v=>{if(typeof v!=='string'||!/^\d{4}-\d\d-\d\d/.test(v))return null;const m=v.slice(10).replace(/^\D*/,'').match(/^\d\d:\d\d/);return m?`${v.slice(0,10)}T${m[0]}`:v.slice(0,10);};
const detection=(app,info={})=>{
 const ids=(Array.isArray(info.title_ids)?info.title_ids:[]).filter(Number.isInteger).slice(0,8);
 const files=(Array.isArray(info.installed_files)?info.installed_files:[]).filter(f=>typeof f==='string'&&Buffer.byteLength(f)<160).slice(0,8);
 if(ids.length||files.length){app.detect={};if(ids.length)app.detect.ids=ids;if(files.length)app.detect.files=files;}
 const at=stamp(info.last_updated);if(at)app.updatedAt=at;
};
for(const app of expected){
 const entry=store.storeContent.find(e=>e.info.title===app.title);
 for(const [format,steps] of Object.entries(app.install)){
  const pair=Object.entries(entry||{}).find(([k])=>new RegExp('\\.'+format+'$','i').test(k)&&!/nightly|\[git\]|\[prerelease\]/i.test(k));
  const raw=pair&&(Array.isArray(pair[1])?pair[1]:pair[1].script);
  if(raw?.some(s=>!supported.has(s.type))){app.nativeUnsupported=true;continue;}
  let di=0,ei=0;
  for(const s of raw||[]){
   if(s.type==='downloadFile'||s.type==='downloadRelease'){
    const dst=steps.filter(x=>x.type==='downloadUrl')[di++];
    if(s.type==='downloadRelease')Object.assign(dst,{repo:s.repo,match:s.file,url:''});
   }
   if(s.type==='extractFile'){const dst=steps.filter(x=>x.type==='extractFile')[ei++];if(dst&&s.input)dst.input=s.input;}
  }
 }
 detection(app,entry?.info);
}
const native=JSON.parse(await readFile(nativePath));
assert.deepEqual(native,JSON.parse(JSON.stringify(expected)));
const report={apps:native.length,fields:'All JSON fields and stable ordering equal to the real web converter + existing native installer adaptations',sourceFull:fullPath,sourceStore:storePath};
await writeFile(new URL('../build/gen/universal-parity.json',import.meta.url),JSON.stringify(report,null,2)+'\n');
console.log('PASS:',native.length,'apps; every JSON field and order match the web conversion');
