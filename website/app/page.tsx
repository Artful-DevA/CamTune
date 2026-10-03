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

function releaseLabel(asset: Asset) {
  const n = asset.name.toLowerCase();
  if (asset.type === 'deb') {
    if (n.includes('debian-12')) return 'Debian 12';
    if (n.includes('debian-13')) return 'Debian 13';
    if (n.includes('ubuntu')) return 'Ubuntu';
    return 'Debian / Ubuntu';
  }
  if (asset.type === 'rpm') {
    const fedora = n.match(/fedora-(\d+)/);
    return fedora ? `Fedora ${fedora[1]}` : 'Fedora / RHEL';
  }
  if (asset.type === 'arch') return 'Arch Linux';
  if (asset.type === 'appimage') return 'AppImage';
  return asset.name;
}

function releaseTag(asset: Asset) {
  if (asset.type === 'deb') return 'DEB';
  if (asset.type === 'rpm') return 'RPM';
  if (asset.type === 'arch') return 'ARCH';
  if (asset.type === 'appimage') return 'APP';
  return 'FILE';
}

const features = [
  ['01', 'Camera controls', 'Exposure, focus, gain, white balance, brightness, contrast, saturation, sharpness and other UVC controls exposed by your camera.'],
  ['02', 'Framing', 'Zoom up to 8×, pan, crop, rotation, mirror and flip. Scroll to zoom, drag to move, double-click to reset.'],
  ['03', 'Virtual camera', 'Use CamTune as a camera source in Zoom, Meet, Teams, Discord and OBS, up to 2560×1440 at 60 fps.'],
  ['04', 'Presets', 'Save camera and framing setups and switch between them quickly instead of rebuilding your setup every call.'],
  ['05', 'Backgrounds', 'Blur or replace the background locally. No cloud upload is required for the processing path.'],
  ['06', 'Low latency', 'A latest-frame pipeline avoids building a queue of stale frames, keeping interaction responsive during long calls.'],
];

export default function Home() {
  const [release, setRelease] = useState<ReleaseData | null>(null);
  const [os, setOs] = useState<'linux'|'windows'|'mac'|'unknown'>('unknown');

  useEffect(() => {
    const p = navigator.platform.toLowerCase();
    const ua = navigator.userAgent.toLowerCase();
    setOs(ua.includes('linux') ? 'linux' : ua.includes('windows') ? 'windows' : p.includes('mac') ? 'mac' : 'unknown');
    fetch('/api/releases').then(r => r.json()).then(setRelease).catch(() => null);
  }, []);

  const assets = useMemo(() => release?.assets ?? [], [release]);
  const releaseDownloads = assets.filter(a => ['deb', 'rpm', 'appimage', 'arch'].includes(a.type));
  const anyReleaseDownload = releaseDownloads.length > 0;

  return (
    <main>
      <header className="site-header">
        <div className="wrap nav">
          <a className="brand" href="#top" aria-label="CamTune home">
            <img src="/camtune-logo.svg" alt="" />
            <span>CamTune</span>
          </a>
          <nav className="nav-links" aria-label="Primary navigation">
            <a href="#features">Features</a>
            <a href="#security">Security</a>
            <a href="#download">Download</a>
            <a href={GITHUB} target="_blank" rel="noreferrer">GitHub ↗</a>
          </nav>
        </div>
      </header>

      <section className="hero wrap" id="top">
        <div className="hero-copy">
          <div className="meta-line"><span>Linux</span><span>Qt</span><span>GPL-3.0</span></div>
          <h1>Webcam controls<br/>for Linux.</h1>
          <p className="lead">CamTune gives you proper control over framing, image settings, presets, backgrounds and a virtual camera without turning into a streaming suite.</p>
          <div className="hero-actions">
            <a className={`primary ${!anyReleaseDownload ? 'disabled' : ''}`} href="#download">
              {anyReleaseDownload ? 'Download CamTune' : 'Release publishing soon'}
            </a>
            <a className="text-link" href={GITHUB} target="_blank" rel="noreferrer">Source on GitHub ↗</a>
          </div>
          <p className="support">Ubuntu 22.04+ · Debian 12+ · Fedora 38+</p>
        </div>

        <div className="hero-product" aria-label="CamTune product summary">
          <div className="hero-logo-wrap"><img src="/camtune-logo.svg" alt="CamTune logo" /></div>
          <div className="product-specs">
            <div><span>Input</span><strong>V4L2 / UVC</strong></div>
            <div><span>Output</span><strong>Virtual camera</strong></div>
            <div><span>Max output</span><strong>2560×1440 · 60 fps</strong></div>
            <div><span>Processing</span><strong>Local</strong></div>
          </div>
        </div>
      </section>

      <section className="manifesto">
        <div className="wrap manifesto-inner">
          <p>Not a recorder.</p>
          <p>Not a streaming suite.</p>
          <p>Just the webcam controls Linux should already have.</p>
        </div>
      </section>

      <section className="section wrap" id="features">
        <div className="section-title-row">
          <div><span className="section-kicker">Capabilities</span><h2>What CamTune actually does.</h2></div>
          <p>Native controls first. Everything else stays out of the way.</p>
        </div>
        <div className="feature-list">
          {features.map(([n, title, body]) => (
            <article className="feature-row" key={n}>
              <span className="feature-num">{n}</span>
              <h3>{title}</h3>
              <p>{body}</p>
            </article>
          ))}
        </div>
      </section>

      <section className="tech-section">
        <div className="wrap tech-grid">
          <div>
            <span className="section-kicker">Pipeline</span>
            <h2>Built to stay responsive.</h2>
            <p>CamTune keeps only the newest frame between processing stages instead of letting latency accumulate. Capture and device I/O stay off the UI thread.</p>
          </div>
          <dl className="tech-stats">
            <div><dt>≈2 ms</dt><dd>1080p MJPEG decode*</dd></div>
            <div><dt>≈2 ms</dt><dd>1080p → 720p zoom + color*</dd></div>
            <div><dt>0</dt><dd>RGB round trips in the main processing path</dd></div>
          </dl>
          <small>* Typical repository benchmark on a 4-core laptop CPU using 2 worker threads.</small>
        </div>
      </section>

      <section className="security-section" id="security">
        <div className="wrap security-grid">
          <div>
            <span className="section-kicker">Security & privacy</span>
            <h2>Designed with a small attack surface.</h2>
          </div>
          <div className="security-copy">
            <p>CamTune processes camera data locally. It does not require an account or cloud processing for its core camera pipeline, and the source code is public so users can inspect how it works.</p>
            <p>Security is treated as a design requirement, not a guarantee. Dependencies are kept current, the website uses restrictive browser security headers, and the project avoids unnecessary network-facing components.</p>
            <a href={GITHUB} target="_blank" rel="noreferrer">Inspect the source on GitHub ↗</a>
          </div>
        </div>
        <div className="wrap security-points" aria-label="CamTune security principles">
          <div><strong>Local processing</strong><span>Camera frames stay on your machine for CamTune's processing path.</span></div>
          <div><strong>No account required</strong><span>No login is needed to use the application.</span></div>
          <div><strong>Open source</strong><span>The implementation can be reviewed publicly.</span></div>
          <div><strong>Minimal web surface</strong><span>No database, user accounts or upload backend on the website.</span></div>
        </div>
      </section>

      <section className="section wrap" id="download">
        <div className="section-title-row download-title-row">
          <div><span className="section-kicker">Download</span><h2>Install CamTune.</h2></div>
          {release?.available && <div className="version">Latest release <strong>{release.tag}</strong></div>}
        </div>

        {release === null ? (
          <div className="release-state">Checking GitHub Releases…</div>
        ) : !release.available || !anyReleaseDownload ? (
          <div className="release-state publishing">
            <div><strong>Release downloads are being published.</strong><p>This page is connected directly to GitHub Releases. Download options will appear automatically as release assets are uploaded.</p></div>
            <a href={`${GITHUB}/releases`} target="_blank" rel="noreferrer">View releases ↗</a>
          </div>
        ) : (
          <div className="package-list">
            {releaseDownloads.map(asset => (
              <a className="package-row" href={asset.url} key={asset.name}>
                <span className="pkg-tag">{releaseTag(asset)}</span>
                <span><strong>{releaseLabel(asset)}</strong><small>{asset.name} · {formatSize(asset.size)}</small></span>
                <b>Download ↓</b>
              </a>
            ))}
          </div>
        )}

        <div className="download-foot">
          <span>{os === 'linux' ? 'Linux detected. Choose the matching release asset.' : 'CamTune currently targets Linux.'}</span>
          <a href={`${GITHUB}/releases`} target="_blank" rel="noreferrer">All releases ↗</a>
        </div>
      </section>

      <footer>
        <div className="wrap footer-inner">
          <div className="brand footer-brand"><img src="/camtune-logo.svg" alt="" /><span>CamTune</span></div>
          <p>Native Linux webcam control.</p>
          <div className="footer-links"><a href={GITHUB} target="_blank" rel="noreferrer">GitHub</a><a href={`${GITHUB}/issues`} target="_blank" rel="noreferrer">Issues</a><a href={`${GITHUB}/blob/claude/admiring-meitner-re9fkq/LICENSE`} target="_blank" rel="noreferrer">License</a></div>
        </div>
      </footer>
    </main>
  );
}
