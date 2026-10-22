![Logo](icon.png)

# smbii

SMB server for the Nintendo Wii/vWii, written in C++

This SMB server supports read/write access to the SD card, USB ports and NAND of your Wii, over the network. 

## Usage

1. Download the latest release and place the apps/ folder on your SD card.
    - Please ensure that the themes/ folder is present; SMBii cannot operate without a theme present.
2. Launch it from the Homebrew Channel (v1.0.7 or later recommended)
3. Optional: Configure authentication in the settings. Windows 11 may require this, by default.
4. Connect from the Windows Explorer. Your local IP address and share name should be displayed on your console, 
simply visit \\<your_ip>\<share_name> in Explorer to connect to the Wii. Ensure you're on the same network as the Wii.
You can change the share directory in the settings using your Wiimote.
    - SMB Guest authentication may be required to be enabled in Windows, if authentication is not set up.
5. Optionally, install the channel from the Settings menu for easier access to SMBii.

## Dolphin Emulator note

Windows does not allow you to bind to a port on the default port, 445, which is also the port Windows 
connects to SMB servers with. Because of this, when you run smbii in Dolphin, or if 445 
otherwise cannot be used, port 1337 will be used instead. You can change the port in the settings
to something else if you want to.

Connecting on a non-standard port requires Windows 11 (unfortunately) and also PowerShell:

```ps
New-SmbMapping -LocalPath Z: -RemotePath \\<ip_or_simply_localhost>\wii -TcpPort 1337 -UserName "" -Password ""
```

## Screenshots

![screenshot1](screenshot-01.png)
![screenshot2](screenshot-02.png)

## License/Copying

Copyright (c) 2026 Jacob Nilsson / Forwarder Factory

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.