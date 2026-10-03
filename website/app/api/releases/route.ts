import { NextResponse } from 'next/server';

const REPO = 'Artful-DevA/CamTune';
const RELEASES_URL = `https://github.com/${REPO}/releases`;

type GHAsset = { name: string; browser_download_url: string; size: number };

function classify(name: string) {
  const n = name.toLowerCase();
  if (n.endsWith('.deb')) return 'deb';
  if (n.endsWith('.rpm')) return 'rpm';
  if (n.endsWith('.appimage')) return 'appimage';
  if (n.endsWith('.tar.gz') || n.endsWith('.tgz')) return 'tar';
  if (n.includes('sha256') || n.includes('checksum')) return 'checksum';
  return 'other';
}

export async function GET() {
  try {
    const response = await fetch(`https://api.github.com/repos/${REPO}/releases/latest`, {
      headers: { Accept: 'application/vnd.github+json', 'User-Agent': 'CamTune-Website' },
      next: { revalidate: 300 }
    });

    if (response.status === 404) {
      return NextResponse.json({ available: false, assets: [], releaseUrl: RELEASES_URL }, { headers: { 'Cache-Control': 'public, s-maxage=60, stale-while-revalidate=300' } });
    }
    if (!response.ok) throw new Error(`GitHub returned ${response.status}`);

    const data = await response.json();
    const assets = (data.assets ?? []).map((a: GHAsset) => ({ name: a.name, url: a.browser_download_url, size: a.size, type: classify(a.name) }));
    return NextResponse.json({ available: true, tag: data.tag_name, name: data.name, publishedAt: data.published_at, assets, releaseUrl: data.html_url || RELEASES_URL }, { headers: { 'Cache-Control': 'public, s-maxage=300, stale-while-revalidate=900' } });
  } catch {
    return NextResponse.json({ available: false, assets: [], releaseUrl: RELEASES_URL, error: 'release_lookup_failed' }, { status: 200, headers: { 'Cache-Control': 'public, s-maxage=30' } });
  }
}
