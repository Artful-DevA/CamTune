import { NextRequest, NextResponse } from 'next/server';

const REPO = 'Artful-DevA/CamTune';
const ALLOWED = new Set(['deb', 'rpm', 'appimage']);

function matches(name: string, type: string) {
  const n = name.toLowerCase();
  return type === 'deb' ? n.endsWith('.deb') : type === 'rpm' ? n.endsWith('.rpm') : n.endsWith('.appimage');
}

export async function GET(request: NextRequest, context: { params: Promise<{ type: string }> }) {
  const { type } = await context.params;
  if (!ALLOWED.has(type)) return new NextResponse('Unsupported package type', { status: 404 });

  try {
    const response = await fetch(`https://api.github.com/repos/${REPO}/releases/latest`, {
      headers: { Accept: 'application/vnd.github+json', 'User-Agent': 'CamTune-Website' },
      next: { revalidate: 120 }
    });
    if (!response.ok) return NextResponse.redirect(new URL(`https://github.com/${REPO}/releases`, request.url), 302);
    const data = await response.json();
    const asset = (data.assets ?? []).find((a: { name: string }) => matches(a.name, type));
    if (!asset?.browser_download_url) return NextResponse.redirect(new URL(`https://github.com/${REPO}/releases`, request.url), 302);
    return NextResponse.redirect(asset.browser_download_url, 302);
  } catch {
    return NextResponse.redirect(new URL(`https://github.com/${REPO}/releases`, request.url), 302);
  }
}
