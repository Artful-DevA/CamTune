import type { Metadata } from 'next';
import './globals.css';

export const metadata: Metadata = {
  title: 'CamTune | Native Linux webcam control',
  description: 'Native Linux webcam controls, framing, background effects, presets and a low-latency virtual camera.',
  icons: { icon: '/favicon.png' },
  openGraph: {
    title: 'CamTune',
    description: 'Native Linux webcam control and virtual camera.',
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
