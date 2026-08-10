# Windows bridge for the Ascend host

These scripts configure Windows host B as both:

- a Tailscale subnet router for exactly one Ascend host (`/32`); and
- a restricted HTTP/HTTPS forward proxy for that Ascend host.

The tracked default target is `9.15.144.34`. On first use it is copied to
`C:\ProgramData\VllmStackBridge\bridge-config.json`. Later IP changes update
that runtime file, so the repository checkout remains clean.

## Install on Windows B

Clone this repository, open PowerShell **as Administrator**, and run from the
repository root:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\windows-bridge\Install-All.ps1
```

If B is not already logged into Tailscale, the command prints a browser login
URL. For non-browser enrollment, use an auth key entered through a hidden
prompt (do not put the key in a command or commit it):

```powershell
.\scripts\windows-bridge\Install-All.ps1 -UseAuthKey
```

After first advertisement, approve `9.15.144.34/32` for B in the Tailscale
admin console unless tailnet policy `autoApprovers` already handles it. If
routing does not work immediately after the first install, reboot B once;
Windows persists `IPEnableRouter=1` during setup.

The Tailscale and proxy installers are idempotent. Rerunning them reconciles
the settings and restarts the managed proxy without replacing healthy files.

## Change the Ascend IP

Run this whenever the Ascend host is reinstalled with a new address:

```powershell
.\scripts\windows-bridge\Set-AscendTarget.ps1 -IpAddress 9.15.144.34
```

It discovers B's correct source interface for the new destination, updates the
Tailscale `/32` route, rewrites the proxy ACL/listener and firewall rule, and
restarts the installed proxy. A changed route may need approval in the
Tailscale admin console again.

`-ConfigOnly` changes only the runtime setting. It is intended for preparing B
while the new Ascend address is not routable yet; rerun without that switch
after connectivity is restored.

## Use the proxy from the Ascend host

After installation, B writes the exact values to:

```text
C:\ProgramData\VllmStackBridge\client-proxy.txt
```

On the Ascend Linux host, copy those values or run:

```bash
export HTTP_PROXY=http://<B_LAN_IP>:18080
export HTTPS_PROXY="$HTTP_PROXY"
export NO_PROXY=localhost,127.0.0.1
curl -I https://github.com
git clone https://github.com/MozhiJiawei/vllm-stack-yellow-zone.git
```

The proxy is deliberately not open to the LAN: it binds only B's interface
toward the Ascend host, accepts only the configured Ascend source IP, and the
Windows firewall repeats the same restriction. CONNECT is limited to port 443
and common private/local destinations are blocked.

## Validate the checked-in scripts

This test is read-only and does not require Administrator privileges:

```powershell
.\scripts\windows-bridge\Test-Scripts.ps1
```
