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
  ['01', 'Camera controls', 'Adjust focus, exposure, white balance, brightness, contrast and the controls your webcam already supports.'],
  ['02', 'Framing', 'Zoom, pan, crop, rotate, mirror and flip without digging through another app.'],
  ['03', 'Virtual camera', 'Use CamTune in Zoom, Meet, Teams, Discord and OBS as a normal camera source.'],
  ['04', 'Presets', 'Save setups for different calls, cameras or lighting and switch between them quickly.'],
  ['05', 'Backgrounds', 'Blur or replace your background while keeping the processing on your computer.'],
  ['06', 'Low latency', 'CamTune is built to stay responsive instead of letting delayed frames pile up during long calls.'],
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
          <div className="meta-line"><span>Linux</span><span>Open source</span></div>
          <h1>Webcam controls<br/>for Linux.</h1>
          <p className="lead">CamTune puts framing, image controls, presets, backgrounds and a virtual camera in one focused Linux app.</p>
          <div className="hero-actions">
            <a className={`primary ${!anyReleaseDownload ? 'disabled' : ''}`} href="#download">
              {anyReleaseDownload ? 'Download CamTune' : 'Release publishing soon'}
            </a>
            <a className="text-link" href={GITHUB} target="_blank" rel="noreferrer">View on GitHub ↗</a>
          </div>
          <p className="support">Debian · Fedora · Arch · AppImage</p>
        </div>

        <div className="hero-product" aria-label="CamTune product summary">
          <div className="hero-logo-wrap"><img src="/camtune-logo.svg" alt="CamTune logo" /></div>
          <div className="product-specs">
            <div><span>Works with</span><strong>Linux webcams</strong></div>
            <div><span>Use it in</span><strong>Zoom, Meet, Teams, OBS</strong></div>
            <div><span>Video processing</span><strong>On your computer</strong></div>
          </div>
        </div>
      </section>

      <section className="manifesto">
        <div className="wrap manifesto-inner">
          <p>No account.</p>
          <p>No cloud camera pipeline.</p>
          <p>Just better webcam control on Linux.</p>
        </div>
      </section>

      <section className="section wrap" id="features">
        <div className="section-title-row">
          <div><span className="section-kicker">Features</span><h2>The controls you actually need.</h2></div>
          <p>Enough control to fix your camera setup without turning CamTune into a streaming suite.</p>
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
            <span className="section-kicker">Performance</span>
            <h2>Made to feel immediate.</h2>
            <p>CamTune prioritizes the newest camera frame instead of letting old frames queue up. That helps controls feel responsive even during long calls.</p>
          </div>
          <dl className="tech-stats">
            <div><dt>Local</dt><dd>Video processing stays on your machine.</dd></div>
            <div><dt>Native</dt><dd>Built as a Linux desktop application.</dd></div>
            <div><dt>Fast</dt><dd>Designed around a low-latency camera pipeline.</dd></div>
          </dl>
        </div>
      </section>

      <section className="security-section" id="security">
        <div className="wrap security-grid">
          <div>
            <span className="section-kicker">Security & privacy</span>
            <h2>Privacy first, with less to expose.</h2>
          </div>
          <div className="security-copy">
            <p>Your camera processing happens locally. CamTune does not require an account or send its core camera pipeline to a cloud service.</p>
            <p>Security is treated as a design requirement. The project is open source, keeps the website deliberately simple, and avoids unnecessary network-facing components.</p>
            <a href={GITHUB} target="_blank" rel="noreferrer">Inspect the source on GitHub ↗</a>
          </div>
        </div>
        <div className="wrap security-points" aria-label="CamTune security principles">
          <div><strong>Local by default</strong><span>Camera processing stays on your machine.</span></div>
          <div><strong>No account</strong><span>There is no login required to use CamTune.</span></div>
          <div><strong>Open source</strong><span>The code is public and can be reviewed.</span></div>
        </div>
      </section>

      <section className="section wrap" id="download">
        <div className="section-title-row download-title-row">
          <div><span className="section-kicker">Download</span><h2>Pick your Linux build.</h2></div>
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
                <span><strong>{releaseLabel(asset)}</strong><small>{formatSize(asset.size)}</small></span>
                <b>Download ↓</b>
              </a>
            ))}
          </div>
        )}

        <div className="download-foot">
          <span>{os === 'linux' ? 'Linux detected. Choose the build that matches your distro.' : 'CamTune currently targets Linux.'}</span>
          <a href={`${GITHUB}/releases`} target="_blank" rel="noreferrer">All releases ↗</a>
        </div>
      </section>

      <footer>
        <div className="wrap footer-inner">
          <div className="brand footer-brand"><img src="/camtune-logo.svg" alt="" /><span>CamTune</span></div>
          <p>Better webcam control for Linux.</p>
          <div className="footer-links"><a href={GITHUB} target="_blank" rel="noreferrer">GitHub</a><a href={`${GITHUB}/issues`} target="_blank" rel="noreferrer">Issues</a><a href={`${GITHUB}/blob/claude/admiring-meitner-re9fkq/LICENSE`} target="_blank" rel="noreferrer">License</a></div>
        </div>
      </footer>
    </main>
  );
}
