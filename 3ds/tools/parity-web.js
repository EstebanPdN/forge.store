// Opt-in visual audit helper, imported through the Code Atlas preview.
// Uses the product's native-resolution capture, never a scaled page screenshot.
const wait=ms=>new Promise(r=>setTimeout(r,ms));
let archiveURL=null;
let readerFixtures=null,readerFixtureSources=null;
export async function configureReaderFixtures(base='/docs/native-v2-parity/fixtures/github/'){
 const manifest=await fetch(base+'manifest.json').then(r=>r.json());readerFixtures={};readerFixtureSources=manifest;
 for(const [key,entry] of Object.entries(manifest)){
  const response=await fetch(base+entry.file);if(!response.ok)throw Error('Missing public reader fixture: '+key);
  readerFixtures[key]=entry.file.endsWith('.html')?await response.text():await response.json();
 }
}
export function configureCaptureArchive(url){
 const parsed=new URL(url);
 if(parsed.protocol!=='http:'||!['localhost','127.0.0.1'].includes(parsed.hostname)||parsed.pathname!=='/capture')throw Error('Local capture archive required');
 archiveURL=parsed.href;
}
export async function captureCase(name,output=name,mode='archive'){
 if(mode==='archive'&&!archiveURL)throw Error('Configure capture-archive.py first; audit captures are not downloaded to Downloads');
 const app=window.forge;app.__parityRelease?.();
 const savedInstalled=app.state.installed,savedSD=app.sd.root,savedJobs=app.installer.jobs,savedRunning=app.installer.running;
 const downloadCase=name.startsWith('downloads-');
 const statusCase=name.startsWith('status-')||name==='sd-owner-fixture';
 const treeFixture=name==='detail-install-tree';
 if(treeFixture)app.sd.root={type:'dir',children:{'3ds':{type:'dir',children:{}}}};
 if(statusCase){app.state.installed={...savedInstalled};for(const id of ['fbi','alttp','starwing'])app.state.installed[id]={version:id==='starwing'?'old-test-version':app.catalog.get(id).releases.stable.tag,channel:'stable',format:'3dsx',cia:false,date:'2026-10-02',files:id==='fbi'?['/parity-fixture/demo.3dsx']:[],dirs:[]};}
 app.nativePreview.dialog?.close();app.nativePreview.releaseDialog?.();
 const appCase=name.startsWith('app:')?name.split(':'):name==='status-installed'?['app','fbi','info']:name==='status-update'?['app','starwing','info']:name.startsWith('status-missing')?['app','alttp',name.endsWith('requirements')?'install':'info',name.endsWith('requirements')?'end':null]:null;
 const item=app.catalog.get(appCase?appCase[1]:name==='development'?'sm64ds':name==='incompatible'?'mk64':'alttp');
 if(appCase&&!item)throw Error('Reference app missing: '+appCase[1]);
 if(name==='intro'||name.startsWith('picker-')){
  await app.reset('intro');await wait(3200);
  if(name.startsWith('picker-')){app.scene.showPicker();app.scene.choose(name.slice(7));await wait(1200);}
  for(const e of [...app.scene.top.querySelectorAll('*'),...app.scene.bottom.querySelectorAll('*')])for(const a of e.getAnimations())if(a.effect.getTiming().iterations===Infinity){a.pause();a.currentTime=0;}
 }
 else if(name==='home-en'||name==='home-universal'){
  await app.reset('home');app.scene.banner.pause();app.scene.banner.el.style.setProperty('--px','0');app.scene.banner.el.style.setProperty('--py','0');
  if(name==='home-universal')app.scene.setCategory('universal');
  app.scene.top.querySelectorAll('.bn-bg,.bn-logo-img').forEach(e=>{for(const a of e.getAnimations()){a.pause();a.currentTime=0;}});
 }
 else if(appCase||name.startsWith('detail-')||['confirm','development','incompatible','video-poster','video-paused','video-hidden'].includes(name)){
  const tab=appCase?appCase[2]:name==='detail-shots'||name.startsWith('video-')?'shots':name==='detail-install'||treeFixture?'install':name==='detail-news'?'news':'info';
  await app.reset('detail',{app:item,tab});
  clearInterval(app.scene.carousel);
  if(name.startsWith('video-'))app.scene.showMedia(1,true);
  clearInterval(app.scene.carousel);
  for(const el of app.scene.heroEls||[])if(el.classList.contains('on')){el.style.animation='none';el.style.transform='none';}
  if(name==='confirm'||name==='incompatible')app.scene.openSheet();
  if(name==='video-paused'||name==='video-hidden'){app.scene.startVideo(app.scene.mediaList[1]);await wait(1200);app.scene.playing.video.pause();app.scene.playing.video.currentTime=2;await wait(500);app.scene.showVideoUI(name==='video-paused');}
  }else if(name.startsWith('search:')){
  await app.reset('search');app.scene.setQuery(name.slice(7));
 }else if(downloadCase){
  const ids=['alttp','mk64','fbi','doom','starwing','gdiffuser'],statuses=['running','queued','done','done','failed','cancelled'];
  app.installer.jobs=name==='downloads-empty'?[]:ids.map((id,i)=>{const item=app.catalog.get(id),release=app.catalog.release(item,'stable'),status=name==='downloads-history'&&i<2?'done':statuses[i];return {id:i+1,app:item,release,tag:release.tag,format:'cia',channel:'stable',status,progress:status==='done'?1:i?0:.4,bytesDone:i?7312320:4387392,bytesTotal:7312320,speed:0,eta:Infinity,missing:i===3?[item.requires[0]]:[],warnings:[],steps:[{key:'install.step.download',vars:{name:'zelda3-3ds.cia'},status:status==='running'?'active':'pending'}]};});
  app.installer.running=app.installer.jobs.find(j=>j.status==='running')||null;
  await app.reset('downloads');
  if(name==='downloads-bottom'){app.scene.setFocus(app.scene.items.length-1);await wait(400);app.scene.scroller.scrollTo(1e9,false);}
 }else if(name.startsWith('install-')){
  const status=name.slice(8),item=app.catalog.get('alttp');
  const job={id:-1,app:item,format:'cia',channel:'stable',release:app.catalog.release(item,'stable'),status:['done','missing'].includes(status)?'done':status==='error'?'failed':status==='cancelled'?'cancelled':status==='queued'?'queued':'running',progress:['done','missing'].includes(status)?1:status==='queued'?0:.4,bytesDone:Math.round(7312320*.6),bytesTotal:7312320,speed:0,eta:Infinity,missing:status==='missing'?[item.requires[0]]:[],warnings:[],created:[],dirs:[]};
  job.steps=app.installer.plan(job);job.steps.forEach((s,i)=>{s.weight=1;s.status=job.status==='done'?'done':job.status==='queued'?'pending':i<3?'done':i===3?(status==='error'?'error':'active'):'pending';s.progress=s.status==='done'?1:s.status==='active'?.6:0;});
  if(status==='missing')job.steps[7].status='warn';
  await app.reset('install',{job});if(['done','missing','error','cancelled'].includes(status))app.scene.finish();
 }else if(name.startsWith('github-')){
  const base=name.replace(/-end$/,'');const user=['github-profile','github-repos','github-about'].includes(base);
  await app.reset('github',user?{user:'EstebanPdN'}:{repo:'EstebanPdN/zelda-alttp-3ds'});
  await wait(2500);
  // Explicit audit snapshots keep dynamic GitHub commits/repos identical to
  // the public responses seeded into the native test SD. Production is untouched.
  if(readerFixtures){
   const scene=app.scene;
   scene.data=user?{user:readerFixtures.user,readme:readerFixtures.profileReadme,repos:readerFixtures.repos.filter(r=>!r.fork)}:{repo:readerFixtures.repo,readme:readerFixtures.readme,releases:readerFixtures.releases,commits:readerFixtures.commits};
   scene.renderTab();
  }
  const tab=base==='github-commits'?'info':base.split('-')[1];if(app.scene.tabs.includes(tab))app.scene.setTab(tab);
  const data=app.scene.data,key=['readme','profile'].includes(tab)?'readme':['releases','repos'].includes(tab)?tab:user?'user':'repo';
  if(!data[key])throw Error('GitHub reference is not ready: '+name);
 }else if(['sd-fixture','sd-file-fixture','sd-empty-fixture','sd-return-fixture','sd-owner-fixture'].includes(name)){
  const file=size=>({type:'file',size,date:'2026-10-02',kind:'other'}),dir=children=>({type:'dir',children,date:'2026-10-02',kind:'other'});
  app.sd.root=dir({'parity-fixture':dir({Alpha:dir({}),Beta:dir({}),'demo.3dsx':file(1024),'notes.txt':file(3072),'picture.png':file(4096)})});
  if(name==='sd-owner-fixture')app.sd.root.children['parity-fixture'].children={'demo.3dsx':{...file(1024),by:'fbi',kind:'store'}};
  await app.reset('sd');app.scene.path=name==='sd-empty-fixture'?'/parity-fixture/Alpha':'/parity-fixture';app.scene.render();if(name==='sd-file-fixture')app.scene.setFocus(2);
 }else if(name==='languages'){await app.reset('settings');app.scene.openLanguages();}
 else await app.reset(name);
 if(app.scene.banner){app.scene.banner.pause();app.scene.banner.el.style.setProperty('--px','0');app.scene.banner.el.style.setProperty('--py','0');for(const e of app.scene.top.querySelectorAll('.bn-bg,.bn-logo-img,.bn-wordmark'))for(const a of e.getAnimations()){if(a.effect.getTiming().iterations===Infinity){a.pause();a.currentTime=0;}}}
 await wait(250);
 if(treeFixture){app.scene.scroller.scrollTo(100,false);await wait(100);}
 if(name==='github-readme-table'){const table=app.scene.top.querySelector('table');if(!table)throw Error('Reference table unavailable');app.scene.scroller.scrollTo(app.scene.scroller.y+table.getBoundingClientRect().top-app.scene.view.getBoundingClientRect().top,false);await wait(100);}
 if(appCase?.[3]==='end'||name==='github-commits'||name.startsWith('github-')&&name.endsWith('-end')){app.scene.scroller.scrollTo(1e9,false);await wait(100);}
 const visible=[app.scene.top,app.scene.bottom].flatMap(screen=>{const viewport=screen.getBoundingClientRect();return [...screen.querySelectorAll('img')].filter(img=>{const r=img.getBoundingClientRect();return r.top<viewport.bottom&&r.bottom>viewport.top&&r.left<viewport.right&&r.right>viewport.left;});});
 visible.forEach(img=>img.loading='eager');
 await Promise.all(visible.map(img=>img.complete?Promise.resolve():new Promise(resolve=>{img.addEventListener('load',resolve,{once:true});img.addEventListener('error',resolve,{once:true});setTimeout(resolve,3000);})));
 const failed=visible.filter(img=>!img.complete||!img.naturalWidth);if(failed.length)throw Error('Visible reference image not ready: '+failed.map(i=>i.src).join(', '));
 const mirrors=[];
 // Some remote icons display in <img> but disallow cross-origin canvas export.
 // Independently fetched, byte-verified source mirrors change the capture
 // origin only; the manifest records hashes and comparison to bundled sources.
 const sourceMirrors=name==='home-universal'?await fetch('/docs/native-v2-parity/web-reference/source-mirrors.json').then(r=>r.json()):[];
 for(const img of visible){const mirror=sourceMirrors.find(m=>m.url===img.src&&m.local);if(mirror){
  mirrors.push({img,src:img.src});img.src=mirror.local;await img.decode();
 }}
 const readState=()=>{ const selected=app.scene.item||app.scene.params.app||app.scene.gridApps?.[app.scene.index];
 return {scenario:name,output,scene:app.scene.constructor.name,app:selected?.id||null,releaseTag:selected?app.catalog.release(selected,app.scene.channel)?.tag:null,tab:app.scene.tab||null,query:app.scene.query,queue:downloadCase?app.scene.items.map(j=>({app:j.app.id,status:j.status,missing:j.missing.length})):null,focus:app.scene.focus,selection:{format:app.scene.format,channel:app.scene.channel},scroll:app.scene.scroller?.y,assetMirrors:mirrors.map(({src})=>src),media:{index:app.scene.mediaIndex,playing:!!app.scene.playing,time:app.scene.playing?.video.currentTime,paused:app.scene.playing?.video.paused,ui:app.scene.videoUI},renderTheme:document.documentElement.dataset.theme,settings:{...app.state.settings},readerFixtureSources:name.startsWith('github-')?readerFixtureSources:null,catalogCount:app.catalog.apps.length,capturedAt:new Date().toISOString()};
};
 if(mode==='compositor'){
  await document.fonts.ready;await wait(600);
  const paused=[];for(const screen of Object.values(app.screens))for(const a of screen.getAnimations({subtree:true}))if(a.playState==='running'){if(Number.isFinite(a.effect.getComputedTiming().endTime))a.finish();else{a.pause();paused.push(a);}}
  await new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r)));
  const rects={};for(const [screen,width] of [['top',400],['bottom',320]]){const r=app.screens[screen].getBoundingClientRect();if(r.width!==width||r.height!==240||!Number.isInteger(r.x)||!Number.isInteger(r.y))throw Error('Compositor capture requires integer 1× native screens');rects[screen]={x:r.x,y:r.y,width:r.width,height:r.height};}
  app.__parityRelease=()=>{paused.forEach(a=>a.play());mirrors.forEach(({img,src})=>img.src=src);if(statusCase||treeFixture){app.state.installed=savedInstalled;app.sd.root=savedSD;}if(downloadCase){app.installer.jobs=savedJobs;app.installer.running=savedRunning;}app.__parityRelease=null;};
  return {...readState(),captureMethod:'Code Atlas compositor, native 1 CSS pixel per output pixel; embedded ICC converted to sRGB',rects,viewport:{width:innerWidth,height:innerHeight},dpr:devicePixelRatio};
 }
 try{await app.nativePreview.inspect();}finally{mirrors.forEach(({img,src})=>img.src=src);}
 const links=[...document.querySelectorAll('.pixel-download')];
 if(links.length!==2)throw Error('Capture did not produce both screens: '+name);
 const state={...readState(),captureMethod:'NativePreview SVG at native resolution; Markdown block boxes taken from the live DOM',captureLayout:app.nativePreview.lastCapture};
 try{
  if(mode==='archive'){
   const response=await fetch(archiveURL,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({name:output,top:links[0].href,bottom:links[1].href,state})});
   if(!response.ok)throw Error('Capture archive failed: '+await response.text());
  }else{
   links.forEach((link,i)=>{link.download='parity-'+output+'-'+(i?'bottom':'top')+'.png';link.click();});
   const meta=document.createElement('a'),url=URL.createObjectURL(new Blob([JSON.stringify(state,null,2)+'\n'],{type:'application/json'}));meta.href=url;meta.download='parity-'+output+'.state.json';meta.click();setTimeout(()=>URL.revokeObjectURL(url),1000);
  }
  await wait(100);
 }finally{
  app.nativePreview.dialog.close();app.nativePreview.releaseDialog?.();
  if(statusCase||treeFixture){app.state.installed=savedInstalled;app.sd.root=savedSD;}
  if(downloadCase){app.installer.jobs=savedJobs;app.installer.running=savedRunning;}
 }
 return {name,scene:app.scene.constructor.name,top:links[0].href.length,bottom:links[1].href.length};
}
