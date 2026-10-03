import type { Metadata } from 'next';
import './globals.css';

export const metadata: Metadata = {
  title: 'CamTune | Native Linux webcam control',
  description: 'Native Linux webcam controls, framing, background effects, presets and a low-latency virtual camera.',
  icons: { icon: '/favicon.svg' },
  openGraph: {
    title: 'CamTune',
    description: 'Your webcam, actually under control.',
    type: 'website',
  },
};

export default function RootLayout({ children }: Readonly<{ children: React.ReactNode }>) {
  return (
    <html lang="en">
      <body>{children}</body>
    </html>
  );
}
