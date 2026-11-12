// Run in the Code Atlas preview. Renders the product's exported SVG in its own
// document, then checks its block boxes against the actual source DOM.
export async function checkCaptureFlow(module) {
 const scene=forge.scene,source=forge.screens.top;
 const {source:svg,boxes}=await module.nativeScreenSvg(source,400,{width:400,height:240,fontEmbedCSS:' ',style:{position:'relative',left:'0',top:'0',margin:'0',zoom:'1',transform:'none',borderRadius:'0'}});
 const xml=decodeURIComponent(svg.slice(svg.indexOf(',')+1));
 const frame=document.createElement('iframe'),url=URL.createObjectURL(new Blob([xml],{type:'image/svg+xml'}));
 frame.style='position:absolute;left:-10000px;top:0;border:0;width:400px;height:240px;';
 try{
  const loaded=new Promise((resolve,reject)=>{frame.onload=resolve;frame.onerror=reject;});frame.src=url;document.body.append(frame);await loaded;
  await new Promise(r=>setTimeout(r,100));
  const doc=frame.contentDocument,root=doc.documentElement.getBoundingClientRect(),failures=[];
  for(const box of boxes){
   const e=doc.querySelector('[data-capture-flow="'+box.id+'"]');
   if(!e){failures.push({id:box.id,missing:true});continue;}
   const r=e.getBoundingClientRect(),errors=[r.x-root.x-box.screenX,r.y-root.y-box.screenY,r.width-box.width,r.height-box.height];
   if(errors.some(x=>Math.abs(x)>.05))failures.push({id:box.id,tag:e.tagName,errors,display:frame.contentWindow.getComputedStyle(e).display,position:frame.contentWindow.getComputedStyle(e).position,parent:e.parentElement.outerHTML.slice(0,120),style:e.getAttribute('style').slice(-600)});
  }
  if(source.querySelector('[data-capture-flow]'))throw Error('Capture left temporary attributes in the UI');
  return {scene:scene.constructor.name,tab:scene.tab,scroll:scene.scroller?.y,blocks:boxes.length,failures,passed:!failures.length};
 }finally{frame.remove();URL.revokeObjectURL(url);}
}
