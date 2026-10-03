'use client';

import { useEffect, useMemo, useState } from 'react';

type Asset = { name: string; url: string; size: number; type: string };
type ReleaseData = { available: boolean; tag?: string; name?: string; publishedAt?: string; assets?: Asset[]; releaseUrl: string };

const GITHUB = 'https://github.com/Artful-DevA/CamTune';

function formatSize(bytes: number) {
  if (!bytes) return '';
  const units = ['B', 'KB', 'MB', 'GB'];
  let value = bytes;
  let i = 0;
  while (value >= 1024 && i < units.length - 1) { value /= 1024; i++; }
  return `${value.toFixed(i > 1 ? 1 : 0)} ${units[i]}`;
}

function AppPreview() {
  return (
    <div className="app-shell" aria-label="Stylized preview of the CamTune application">
      <div className="app-titlebar"><span className="dot"/><span>CamTune</span><span className="window-actions">— □ ×</span></div>
      <div className="app-toolbar">
        <span className="select">Integrated Camera ▾</span><span className="select">Normal ▾</span><button className="vcam-on">● Virtual camera</button>
      </div>
      <div className="app-main">
        <div className="preview-pane">
          <div className="camera-frame"><div className="person"><div className="head"/><div className="body"/></div><div className="focus-corner tl"/><div className="focus-corner tr"/><div className="focus-corner bl"/><div className="focus-corner br"/></div>
          <div className="preview-controls"><span>−</span><div className="range"><i/></div><span>+</span><b>1.25×</b><span className="pill">Mirror</span><span className="pill">Fit</span></div>
        </div>
        <div className="inspector">
          <div className="rail"><b>◐</b><span>⌗</span><span>✦</span><span>◉</span><span>↗</span></div>
          <div className="panel"><p className="eyebrow">PICTURE</p>{['Brightness','Contrast','Saturation','Warmth'].map((x,i)=><div className="control" key={x}><label>{x}<small>{[0,12,5,-3][i]}</small></label><div className="range small"><i style={{width:`${[50,64,57,46][i]}%`}}/></div></div>)}<p className="eyebrow gap">FRAMING</p><div className="control"><label>Zoom <small>1.25×</small></label><div className="range small"><i style={{width:'38%'}}/></div></div></div>
        </div>
      </div>
      <div className="status"><span><i className="green"/> Camera ready · 1920×1080 @ 30 fps</span><span>Processing 4.2 ms</span></div>
    </div>
  );
}

export default function Home() {
  const [release, setRelease] = useState<ReleaseData | null>(null);
  const [os, setOs] = useState<'linux'|'windows'|'mac'|'unknown'>('unknown');

  useEffect(() => {
    const p = navigator.platform.toLowerCase();
    const ua = navigator.userAgent.toLowerCase();
    setOs(ua.includes('linux') ? 'linux' : ua.includes('windows') ? 'windows' : p.includes('mac') ? 'mac' : 'unknown');
    fetch('/api/releases').then(r => r.json()).then(setRelease).catch(() => null);
  }, []);

  const packageAssets = useMemo(() => release?.assets ?? [], [release]);
  const deb = packageAssets.find(a => a.type === 'deb');
  const rpm = packageAssets.find(a => a.type === 'rpm');
  const appImage = packageAssets.find(a => a.type === 'appimage');
  const anyPackage = deb || rpm || appImage;

  return (
    <main>
      <nav className="nav wrap"><a className="brand" href="#top"><img className="brand-logo" src="/camtune-logo.png" alt="" />CamTune</a><div className="nav-links"><a href="#features">Features</a><a href="#download">Download</a><a href={`${GITHUB}#building`} target="_blank">Build</a><a className="github-link" href={GITHUB} target="_blank">GitHub ↗</a></div></nav>

      <section className="hero wrap" id="top">
        <div className="hero-copy">
          <div className="kicker"><span/> Native Linux webcam control</div>
          <h1>Your webcam.<br/><em>Actually under control.</em></h1>
          <p className="lead">Framing, color, presets, background effects and a low-latency virtual camera, in one focused native Linux app.</p>
          <div className="hero-actions">
            <a className={`primary ${!anyPackage ? 'disabled' : ''}`} href="#download">{anyPackage ? 'Download for Linux' : 'Packages publishing soon'} <span>↓</span></a>
            <a className="secondary" href={GITHUB} target="_blank">View source <span>↗</span></a>
          </div>
          <p className="support">Ubuntu 22.04+ · Debian 12+ · Fedora 38+ · GPL-3.0</p>
        </div>
        <div className="hero-visual"><div className="halo"/><AppPreview/></div>
      </section>

      <section className="trust-strip"><div className="wrap strip-inner"><span>Native Qt application</span><span>Local processing</span><span>V4L2 / UVC controls</span><span>Open source</span></div></section>

      <section className="section wrap" id="features">
        <div className="section-head"><div><p className="eyebrow teal">WHAT CAMTUNE DOES</p><h2>Everything between your camera<br/>and your call.</h2></div><p className="section-note">Not a recorder. Not a streaming suite. Just the controls Linux webcam users should already have.</p></div>
        <div className="feature-grid">
          <article className="feature big"><div className="feature-icon">⌗</div><h3>Frame it exactly right.</h3><p>Smooth zoom up to 8×, pan, crop, rotation, mirror, flip and aspect controls. Scroll to zoom. Drag to pan. Double-click to reset.</p><div className="framing-demo"><div className="crop-box"><span>1.75×</span><div className="cross x"/><div className="cross y"/></div></div></article>
          <article className="feature"><div className="feature-icon">◐</div><h3>Real camera controls.</h3><p>Exposure, focus, gain, white balance, brightness, contrast, saturation, sharpness and whatever else your UVC camera exposes.</p></article>
          <article className="feature"><div className="feature-icon">▣</div><h3>A virtual camera apps can see.</h3><p>Send CamTune into Zoom, Teams, Meet, Discord or OBS from 360p through 1440p at up to 60 fps.</p></article>
          <article className="feature"><div className="feature-icon">✦</div><h3>Background effects, locally.</h3><p>Blur or replace your background with a compact built-in segmentation model. No cloud processing required.</p></article>
          <article className="feature"><div className="feature-icon">⚡</div><h3>Built for long calls.</h3><p>A latest-frame pipeline drops stale work instead of letting latency grow. Capture and device I/O stay off the UI thread.</p></article>
        </div>
      </section>

      <section className="performance"><div className="wrap perf-grid"><div><p className="eyebrow teal">DESIGNED FOR LOW LATENCY</p><h2>Fast because the pipeline stays simple.</h2><p>CamTune works in YUV, uses pooled frames, keeps only the newest frame between stages and writes directly to the virtual camera.</p></div><div className="metrics"><div><strong>≈2 ms</strong><span>1080p MJPEG decode*</span></div><div><strong>≈2 ms</strong><span>1080p → 720p zoom + color*</span></div><div><strong>0</strong><span>RGB round trips in the processing path</span></div></div><small>* Typical repo benchmark on a 4-core laptop CPU using 2 worker threads.</small></div></section>

      <section className="section wrap download-section" id="download">
        <div className="section-head download-head"><div><p className="eyebrow teal">DOWNLOAD</p><h2>Get CamTune.</h2></div>{release?.available && <div className="version-badge">Latest release <b>{release.tag}</b></div>}</div>
        {release === null ? <div className="download-state">Checking GitHub Releases…</div> : !release.available || !anyPackage ? (
          <div className="coming-soon"><div><span className="pulse"/><h3>Packages are being published.</h3><p>The site is already connected to GitHub Releases. As soon as `.deb`, `.rpm` or AppImage assets appear in a release, the correct download options will show here automatically.</p></div><a className="secondary" href={`${GITHUB}/releases`} target="_blank">Watch releases ↗</a></div>
        ) : (
          <div className="downloads">
            {deb && <a className="package-card recommended" href="/download/deb"><div><span className="pkg-icon">DEB</span><div><b>Ubuntu / Debian</b><small>{deb.name} · {formatSize(deb.size)}</small></div></div><span>Download ↓</span></a>}
            {rpm && <a className="package-card" href="/download/rpm"><div><span className="pkg-icon">RPM</span><div><b>Fedora / RHEL</b><small>{rpm.name} · {formatSize(rpm.size)}</small></div></div><span>Download ↓</span></a>}
            {appImage && <a className="package-card" href="/download/appimage"><div><span className="pkg-icon">APP</span><div><b>AppImage</b><small>{appImage.name} · {formatSize(appImage.size)}</small></div></div><span>Download ↓</span></a>}
          </div>
        )}
        <div className="download-foot"><span>{os === 'linux' ? 'Linux detected. Choose your distro package above.' : 'CamTune currently supports Linux.'}</span><a href={`${GITHUB}/releases`} target="_blank">All releases on GitHub ↗</a></div>
      </section>

      <section className="cta"><div className="wrap cta-inner"><div><p className="eyebrow">OPEN SOURCE</p><h2>Inspect it. Build it.<br/>Make it better.</h2></div><div><p>CamTune is GPL-3.0-or-later and developed in public.</p><a className="primary dark" href={GITHUB} target="_blank">Open GitHub <span>↗</span></a></div></div></section>
      <footer className="wrap footer"><a className="brand" href="#top"><img className="brand-logo" src="/camtune-logo.png" alt="" />CamTune</a><p>Native Linux webcam control and virtual camera.</p><div><a href={GITHUB} target="_blank">GitHub</a><a href={`${GITHUB}/issues`} target="_blank">Issues</a><a href={`${GITHUB}/blob/main/LICENSE`} target="_blank">GPL-3.0</a></div></footer>
    </main>
  );
}
