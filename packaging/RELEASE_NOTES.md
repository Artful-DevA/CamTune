## Install

Download the file for your system from **Assets** below, then:

| System | File | Install |
|---|---|---|
| Ubuntu 26.04 | `camtune_*_ubuntu-26.04_amd64.deb` | `sudo apt install ./camtune_*_ubuntu-26.04_amd64.deb` |
| Ubuntu 24.04 · Linux Mint 22 · Pop!_OS 24.04 · Zorin 18 | `camtune_*_ubuntu-24.04_amd64.deb` | `sudo apt install ./camtune_*_ubuntu-24.04_amd64.deb` |
| Ubuntu 22.04 · Linux Mint 21 · Pop!_OS 22.04 · Zorin 17 | `camtune_*_ubuntu-22.04_amd64.deb` | `sudo apt install ./camtune_*_ubuntu-22.04_amd64.deb` |
| Debian 13 (trixie) · LMDE 7 | `camtune_*_debian-13_amd64.deb` | `sudo apt install ./camtune_*_debian-13_amd64.deb` |
| Debian 12 (bookworm) · LMDE 6 | `camtune_*_debian-12_amd64.deb` | `sudo apt install ./camtune_*_debian-12_amd64.deb` |
| Fedora 44 | `camtune-*-fedora-44.x86_64.rpm` | `sudo dnf install ./camtune-*-fedora-44.x86_64.rpm` |
| Fedora 43 | `camtune-*-fedora-43.x86_64.rpm` | `sudo dnf install ./camtune-*-fedora-43.x86_64.rpm` |
| Arch Linux · EndeavourOS · Manjaro | `camtune-*-x86_64.pkg.tar.zst` | `sudo pacman -U ./camtune-*-x86_64.pkg.tar.zst` |
| Anything else | `CamTune-*-x86_64.AppImage` | `chmod +x CamTune-*.AppImage && ./CamTune-*.AppImage` |

Run the install command in the folder you downloaded to (usually `cd ~/Downloads`).
Use `apt install ./file.deb` rather than double-clicking, so missing dependencies
are installed too.

Then open **CamTune** from your app menu and choose **Tools → Set Up Virtual
Camera…** once. Fedora needs the free [RPM Fusion](https://rpmfusion.org/Configuration)
repository for the virtual camera module.

`SHA256SUMS` lists checksums for every file (`sha256sum -c SHA256SUMS --ignore-missing`).
