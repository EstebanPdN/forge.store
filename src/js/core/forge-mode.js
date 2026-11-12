// The same authenticated envelope is consumed by the native installer. Only the
// derived key is retained locally; plaintext URLs never enter saved app state.
import { Emitter } from './state.js';
const encoder = new TextEncoder();
const aad = encoder.encode('forge.store/private-assets/v1');
const storageKey = 'forge.store.private-key.v1';
const toBase64 = (bytes) => btoa(String.fromCharCode(...bytes));
const fromBase64 = (s) => Uint8Array.from(atob(s), (c) => c.charCodeAt(0));

class ForgeMode extends Emitter {
  constructor() {
    super();
    this.status = 'locked';
    this.manifest = null;
    this.ready = this.restore();
  }
  setStatus(value) { this.status = value; this.emit('change', value); }
  get enabled() { return this.status === 'enabled' && !!this.manifest; }
  async envelope() {
    if (this.blob) return this.blob;
    const response = await fetch(new URL('../../../assets/config/personal-assets.fme', import.meta.url));
    if (!response.ok) throw new Error('Envelope unavailable');
    const bytes = new Uint8Array(await response.arrayBuffer());
    const view = new DataView(bytes.buffer);
    if (bytes.length < 56 || bytes.length > 16440 || new TextDecoder().decode(bytes.slice(0, 4)) !== 'FM01' || view.getUint32(52, true) !== bytes.length - 56) throw new Error('Invalid envelope');
    this.iterations = view.getUint32(4, true);
    if (this.iterations < 10000 || this.iterations > 200000) throw new Error('Invalid derivation cost');
    this.hash = new Uint8Array(await crypto.subtle.digest('SHA-256', bytes));
    return (this.blob = bytes);
  }
  async decrypt(rawKey) {
    const bytes = await this.envelope();
    const key = await crypto.subtle.importKey('raw', rawKey, 'AES-GCM', false, ['decrypt']);
    const sealed = new Uint8Array(bytes.length - 40);
    sealed.set(bytes.slice(56)); sealed.set(bytes.slice(36, 52), bytes.length - 56);
    const clear = new Uint8Array(await crypto.subtle.decrypt({ name: 'AES-GCM', iv: bytes.slice(24, 36), additionalData: aad, tagLength: 128 }, key, sealed));
    try {
      const data = JSON.parse(new TextDecoder().decode(clear));
      for (const [id, steps] of Object.entries(data)) {
        if (!/^[a-z0-9_-]{1,64}$/i.test(id) || !Array.isArray(steps) || steps.length > 4) throw new Error('Invalid entry');
        for (const step of steps) if (step.type !== 'downloadUrl' || step.personal !== true || !step.url?.startsWith('https://') || !step.output?.startsWith('/3ds/') || step.output.includes('..') || !/^[0-9a-f]{64}$/i.test(step.sha256) || !(step.size > 0 && step.size <= 134217728)) throw new Error('Invalid resource');
      }
      return data;
    } finally { clear.fill(0); }
  }
  async restore() {
    let saved;
    try {
      const value = localStorage.getItem(storageKey);
      if (!value) return;
      saved = fromBase64(value); await this.envelope();
      if (saved.length !== 68 || new TextDecoder().decode(saved.slice(0, 4)) !== 'FMK1' || this.hash.some((v, i) => v !== saved[i + 4])) return;
      this.manifest = await this.decrypt(saved.slice(36)); this.setStatus('enabled');
    } catch { this.setStatus('locked'); }
    finally { saved?.fill(0); }
  }
  async unlock(code) {
    await this.ready;
    if (this.status === 'checking' || this.enabled || !code || code.length > 64) return false;
    this.setStatus('checking'); let raw;
    try {
      const bytes = await this.envelope();
      const source = await crypto.subtle.importKey('raw', encoder.encode(code), 'PBKDF2', false, ['deriveBits']); code = '';
      raw = new Uint8Array(await crypto.subtle.deriveBits({ name: 'PBKDF2', hash: 'SHA-256', salt: bytes.slice(8, 24), iterations: this.iterations }, source, 256));
      const manifest = await this.decrypt(raw);
      const saved = new Uint8Array(68); saved.set(encoder.encode('FMK1')); saved.set(this.hash, 4); saved.set(raw, 36);
      try { localStorage.setItem(storageKey, toBase64(saved)); }
      catch { this.setStatus('storage'); return false; }
      finally { saved.fill(0); }
      this.manifest = manifest; this.setStatus('enabled'); return true;
    } catch { this.setStatus('invalid'); return false; }
    finally { raw?.fill(0); }
  }
  plan(id, base) { return [...(this.enabled ? this.manifest[id] || [] : []), ...base].map((s) => ({ ...s })); }
}
export const forgeMode = new ForgeMode();
