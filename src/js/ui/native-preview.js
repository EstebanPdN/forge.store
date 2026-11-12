// Native-size view plus an explicit raster inspection of the current screen.
// The image buffers are always 400×240 / 320×240, even on a Retina display.
import { h } from '../core/dom.js';
import '../vendor/html-to-image.js';

// Embed only our local UI fonts. Scanning imported stylesheets can resolve a
// relative @import against the wrong base, and makes the capture depend on CSSOM.
let embeddedFonts;
function captureFonts() {
  if (!embeddedFonts) embeddedFonts = (async () => {
    const dataURL = async (url) => {
      const response = await fetch(url);
      if (!response.ok) throw new Error(`Font capture: ${response.status}`);
      const blob = await response.blob();
      return new Promise((resolve, reject) => {
        const reader = new FileReader();
        reader.onload = () => resolve(reader.result);
        reader.onerror = () => reject(reader.error);
        reader.readAsDataURL(blob);
      });
    };
    const base = new URL('../../../assets/fonts/', import.meta.url);
    const [inter, dmResponse] = await Promise.all([
      dataURL(new URL('inter/Inter-var.ttf', base)), fetch(new URL('dmsans.css', base)),
    ]);
    let dm = await dmResponse.text();
    for (const path of new Set([...dm.matchAll(/url\(([^)]+)\)/g)].map((m) => m[1]))) {
      dm = dm.replaceAll(`url(${path})`, `url(${await dataURL(new URL(path, base))})`);
    }
    return `@font-face{font-family:Inter;src:url(${inter});font-style:normal;font-weight:100 900;}\n${dm}`;
  })().catch((error) => { embeddedFonts = null; throw error; });
  return embeddedFonts;
}

// SVG export freezes computed heights. That changes collapsed margins inside
// GitHub's nested heading/article wrappers. Keep the browser's actual block
// boxes for this reader; inline text still uses its original fonts and wrapping.
export function preserveCaptureFlow(svgURL,boxes=[]) {
  const comma=svgURL.indexOf(',');
  const doc=new DOMParser().parseFromString(decodeURIComponent(svgURL.slice(comma+1)),'image/svg+xml');
  if(doc.querySelector('parsererror'))throw new Error('Invalid capture SVG');
  for(const box of boxes){
    let element=doc.querySelector('[data-capture-flow="'+box.id+'"]');
    if(!element)throw new Error('Missing capture block: '+box.id);
    if(element.localName.includes('-')){
      const block=doc.createElementNS('http://www.w3.org/1999/xhtml','div');
      for(const attribute of element.attributes)block.setAttribute(attribute.name,attribute.value);
      while(element.firstChild)block.appendChild(element.firstChild);
      element.parentNode.replaceChild(block,element);element=block;element.style.display='block';
    }
    // A positioned HTML table has a separate anonymous table wrapper in an
    // SVG document. Position an ordinary block and keep its table in normal flow.
    if(element.localName==='table'){
      const table=element,wrapper=doc.createElementNS('http://www.w3.org/1999/xhtml','div');
      wrapper.style.cssText=table.style.cssText;wrapper.style.display='block';
      wrapper.setAttribute('data-capture-flow',box.id);table.removeAttribute('data-capture-flow');
      table.parentNode.replaceChild(wrapper,table);wrapper.appendChild(table);
      table.style.position='static';table.style.margin='0';table.style.marginBlock='0';
      table.style.width=table.style.inlineSize='100%';table.style.height=table.style.blockSize='auto';
      element=wrapper;
    }
    const parent=element.parentElement;
    if(!parent.style.position||parent.style.position==='static')parent.style.position='relative';
    const style=element.style;
    style.position='absolute';style.margin='0';style.marginBlock='0';style.marginInline='0';
    style.boxSizing='border-box';
    style.left=style.insetInlineStart=box.x+'px';style.top=style.insetBlockStart=box.y+'px';
    style.right=style.insetInlineEnd='auto';style.bottom=style.insetBlockEnd='auto';
    style.width=style.inlineSize=box.width+'px';style.height=style.blockSize=box.height+'px';

  }
  return 'data:image/svg+xml;charset=utf-8,'+encodeURIComponent(new XMLSerializer().serializeToString(doc));
}
export async function nativeScreenSvg(screen,width,options) {
  const screenRect=screen.getBoundingClientRect(),scale=screenRect.width/width,boxes=[],originals=[];
  const elements=[...screen.querySelectorAll('.gh-md,.gh-md *')],flow=new Set();
  for(const element of elements){
    if(element.namespaceURI!=='http://www.w3.org/1999/xhtml')continue;
    const display=getComputedStyle(element).display;
    // Inline <picture> wrappers can have a 13px line box around a 185px image.
    // Freeze the image's actual ink box too; positioning only its wrapper would
    // move the image to the middle of that line box in the SVG document.
    if((!['block','list-item','flex','grid'].includes(display)&&!element.localName.includes('-')&&element.tagName!=='IMG')||['VIDEO','CANVAS'].includes(element.tagName))continue;
    if(!element.getBoundingClientRect().width||!element.getBoundingClientRect().height)continue;
    flow.add(element);
    // Linked pictures can have an inline custom wrapper. Its anchor also needs
    // its real box once the child leaves normal flow in the export document.
    for(let parent=element.parentElement;parent?.closest('.gh-md');parent=parent.parentElement){
      if(!['inline','inline-block','contents'].includes(getComputedStyle(parent).display))break;
      flow.add(parent);
    }
  }
  for(const element of elements.filter(e=>flow.has(e))){
    const rect=element.getBoundingClientRect(),parent=element.parentElement,parentRect=parent.getBoundingClientRect();
    if(!rect.width||!rect.height)continue;
    const id=String(boxes.length);
    originals.push({element,value:element.getAttribute('data-capture-flow')});
    element.setAttribute('data-capture-flow',id);
    boxes.push({id,screenX:(rect.x-screenRect.x)/scale,screenY:(rect.y-screenRect.y)/scale,x:(rect.x-parentRect.x)/scale-parent.clientLeft,y:(rect.y-parentRect.y)/scale-parent.clientTop,width:rect.width/scale,height:rect.height/scale});
  }
  let source;
  try{source=preserveCaptureFlow(await window.htmlToImage.toSvg(screen,options),boxes);}
  finally{for(const {element,value} of originals)if(value===null)element.removeAttribute('data-capture-flow');else element.setAttribute('data-capture-flow',value);}
  return {source,boxes};
}
async function captureCanvas(screen,width,options) {
  const {source,boxes}=await nativeScreenSvg(screen,width,options);
  const image=new Image();image.src=source;await image.decode();
  await new Promise(resolve=>requestAnimationFrame(resolve));
  const canvas=document.createElement('canvas');canvas.width=width;canvas.height=240;
  canvas.getContext('2d').drawImage(image,0,0,width,240);
  canvas.captureFlowBoxes=boxes;
  return canvas;
}

export class NativePreview {
  constructor(app) {
    this.app = app;
    this.button = document.querySelector('#native-resolution');
    this.note = document.querySelector('#resolution-note');
    this.button.addEventListener('click', () => {
      const enabled = !app.state.get('nativePreview');
      app.state.set('nativePreview', enabled);
      if (enabled) {
        app.state.set('view', 'screens');
        app.state.set('zoom', '1');
      }
    });
    document.querySelector('#inspect-pixels').addEventListener('click', () => this.inspect());
    app.state.on('setting', () => this.sync());
    this.sync();
  }
  sync() {
    const on = !!this.app.state.get('nativePreview');
    this.button.setAttribute('aria-pressed', String(on));
    this.app.device.dataset.native = String(on);
    this.note.textContent = on ? '400 × 240 + 320 × 240 · escala entera · Lupa: píxeles reales' : '';
  }
  async inspect() {
    if (this.dialog?.open) return;
    // A dialog's close event is queued. Finish its cleanup before another
    // inspection can inherit the temporarily disabled input state.
    this.releaseDialog?.();
    if (this.app.device.classList.contains('off')) {
      this.note.textContent = 'Enciende la consola para inspeccionar sus pantallas.';
      return;
    }
    const previousInput = this.app.input.enabled;
    this.app.input.enabled = false;
    const close = h('button', { type: 'button', on: { click: () => dialog.close() } }, 'Volver');
    const status = h('p', { role: 'status' }, 'Preparando la captura a resolución nativa…');
    const controls = h('div.pixel-controls');
    const frames = h('div.pixel-frames');
    const dialog = h('dialog.pixel-dialog', { 'aria-label': 'Inspección a resolución 3DS' },
      h('div.pixel-heading', h('h2', 'Píxeles de la 3DS'), close),
      h('p', 'Captura de la pantalla actual con las transiciones terminadas. La ampliación repite píxeles; no vuelve a dibujar el texto a alta resolución.'),
      status, controls, frames,
    );
    this.dialog = dialog;
    document.body.append(dialog);
    let released = false;
    const release = () => {
      if (released) return;
      released = true;
      this.app.input.enabled = previousInput;
      dialog.remove();
      if (this.dialog === dialog) this.dialog = null;
      document.querySelector('#inspect-pixels').focus();
    };
    this.releaseDialog = release;
    dialog.addEventListener('close', release, { once: true });
    dialog.showModal();
    const paused = [];
    const videoFrames = [];
    try {
      await document.fonts.ready;
      // Let entrance/tab transitions settle before freezing the moving artwork.
      await new Promise((resolve) => setTimeout(resolve, 500));
      if (!dialog.open) return;
      for (const screen of Object.values(this.app.screens)) {
        for (const animation of screen.getAnimations({ subtree: true })) {
          if (animation.playState !== 'running') continue;
          if (Number.isFinite(animation.effect.getComputedTiming().endTime)) animation.finish();
          else { animation.pause(); paused.push(animation); }
        }
      }
      // Let finished entrance animations remove their temporary flashes/layers.
      await new Promise((resolve) => setTimeout(resolve, 0));
      const fontEmbedCSS = await captureFonts();
      // html-to-image draws videos into a resized canvas with smoothing enabled,
      // ignoring image-rendering:pixelated. Freeze the intrinsic frame instead;
      // the replacement image retains the browser's computed layout and sampling.
      // Do not pause the media: pause handlers change the scene's controls/audio.
      for (const screen of Object.values(this.app.screens)) {
        for (const video of screen.querySelectorAll('video')) {
          if (video.readyState < 2 || !video.videoWidth || getComputedStyle(video).display === 'none') continue;
          const frame = document.createElement('canvas');
          frame.width = video.videoWidth;
          frame.height = video.videoHeight;
          frame.getContext('2d').drawImage(video, 0, 0);
          const image = new Image();
          image.className = video.className;
          const style = getComputedStyle(video);
          for (const property of style) image.style.setProperty(property, style.getPropertyValue(property));
          image.src = frame.toDataURL('image/png');
          await image.decode();
          video.replaceWith(image);
          videoFrames.push({ video, image });
        }
      }
      const canvases = [];this.lastCapture=[];
      for (const [name, width] of [['top', 400], ['bottom', 320]]) {
        const screenRect = this.app.screens[name].getBoundingClientRect();
        const canvas = await captureCanvas(this.app.screens[name],width,{
          width, height: 240, canvasWidth: width, canvasHeight: 240, pixelRatio: 1,
          fontEmbedCSS, skipAutoScale: true,
          // An unloaded video has no image to clone. Its empty poster URL would
          // otherwise resolve to index.html; the scene's own poster stays visible.
          filter: (node) => {
            if (node instanceof HTMLElement && (node.hidden || getComputedStyle(node).display === 'none')) return false;
            if (node instanceof HTMLVideoElement && node.readyState < 2) return false;
            // Keep every page's geometry, but do not fetch hundreds of clipped
            // lazy icons. An unavailable icon on page 20 must not break page 1.
            if (node instanceof HTMLImageElement) {
              const page = node.closest('.grid-page');
              if (page) { const r = page.getBoundingClientRect(); if (r.right <= screenRect.left + .01 || r.left >= screenRect.right - .01) return false; }
            }
            return true;
          },
          style: { position: 'relative', left: '0', top: '0', margin: '0', zoom: '1', transform: 'none', borderRadius: '0' },
        });
        canvases.push(canvas);this.lastCapture.push({screen:name,width,height:240,blocks:canvas.captureFlowBoxes});
        const label = `${name === 'top' ? 'Superior' : 'Táctil'} · ${width} × 240`;
        const download = h('a.pixel-download', { href: canvas.toDataURL('image/png'), download: `forge-v2-${name}-${width}x240.png` }, 'Guardar PNG');
        frames.append(h('figure', h('figcaption', label), canvas, download));
      }
      const setScale = (scale) => {
        canvases.forEach((c) => { c.style.width = `${c.width * scale}px`; c.style.height = `${c.height * scale}px`; });
        controls.querySelectorAll('button').forEach((b) => b.setAttribute('aria-pressed', String(Number(b.dataset.scale) === scale)));
      };
      for (const scale of [1, 2, 3]) controls.append(h('button', { type: 'button', dataset: { scale }, on: { click: () => setScale(scale) } }, `${scale}×`));
      setScale(1);
      status.textContent = 'Búfer nativo · sin suavizado al ampliar · Esc para volver';
    } catch (error) {
      status.textContent = 'No se pudo capturar esta pantalla. Vuelve a la vista 3DS e inténtalo de nuevo.';
      console.error('[native-preview]', error);
    } finally {
      videoFrames.forEach(({ video, image }) => image.replaceWith(video));
      paused.forEach((animation) => animation.play());
    }
  }
}
