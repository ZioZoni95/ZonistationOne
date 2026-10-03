# Security

ZoniStation One is an emulator that runs on your own machine, and `deploy/` can publish sessions
from a Kubernetes cluster. Both surfaces matter here: a malformed disc or memory card image reaching
the emulator, and the cluster's session pages, relay and ingress.

## Reporting

Please report a vulnerability privately, through GitHub's
[private vulnerability reporting](https://github.com/ZioZoni95/ZonistationOne/security/advisories/new),
and not in a public issue or pull request. Include the version (`git rev-parse --short HEAD`), what
the problem lets someone do, and steps to reproduce. Do not attach BIOS images or commercial disc
dumps.

## Scope

- Crashes or memory errors triggered by crafted disc, ECM, `.sbi`, memory card or savestate files.
- The cluster deployment: the WebRTC and noVNC session pages, the TURN relay, the ingress and its
  basic-auth credentials, and anything that exposes the read-only BIOS and disc mounts.

Not security issues: emulation inaccuracies and compatibility problems. Use the issue templates.

## Credentials

No credential belongs in this repository. Session passwords and the TURN secret live in the cluster
and in your password manager. If one was ever committed or pasted somewhere public, rotate it.
