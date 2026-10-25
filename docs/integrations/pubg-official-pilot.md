# PUBG: official compatibility inquiry

Research checkpoint: 25 October 2026. This is a draft proposal, not an approved
PUBG integration, a compatibility claim or a message already sent to KRAFTON.

## Entry point and external dependency

Use the official [PUBG Support request form](https://support.pubg.com/hc/en-us/requests/new)
to ask for routing to the PC platform/anti-cheat engineering team. Support is a
verified inquiry channel; it is not evidence that a developer partnership or
compatibility pilot is available. Do not use the site's DSA-only contacts for
this unrelated request. No private account, machine or diagnostic attachments
are necessary for this initial routing question.

KRAFTON's [11 August 2025 technical letter](https://pubg.com/en/news/9001)
describes kernel-based protection. Wine API compatibility does not establish
equivalent security properties or acceptance by a protected service. Valve's
[Proton documentation](https://partner.steamgames.com/doc/steamhardware/proton)
also requires title-specific enablement for supported anti-cheat middleware and
states that kernel-space solutions are unsupported in Proton. Those Linux
instructions do not establish PUBG or macOS support.

## Draft message

Subject: Technical routing request: sanctioned PUBG PC compatibility research on macOS

Hello PUBG Support,

We maintain VibeWine, a community Wine/macOS and DXMT development project:
https://github.com/SlopSurfer4444/VibeWine

We would like to understand whether KRAFTON is willing to discuss an official,
limited PUBG PC compatibility investigation for Apple Silicon/macOS. We are not
claiming that PUBG or its anti-cheat currently works in our environment, and
we are not requesting permission to bypass, disable or impersonate protection.

Could you route this inquiry to the team responsible for PC platform support
and anti-cheat integration, or provide the appropriate developer contact?
Before investing in PUBG-specific engineering, we need to establish:

1. Is macOS through a compatibility layer an environment KRAFTON would consider
   supporting? Please distinguish it from Windows on ARM and virtual machines.
2. Which mandatory protection components and security properties would an
   accepted integration need? Is an officially supported non-Windows component
   or another approved integration path available?
3. If an investigation is possible, can KRAFTON provide an authorized test
   build/environment and written scope, separate from public matchmaking?
4. What functional, security, update and performance acceptance criteria would
   apply, and who could review the resulting evidence?
5. What limitations should be communicated to users while support is absent?

We can share public source changes and small reproducible tests of general
Wine/DXMT behavior. We would agree on any protected tests, data exchange and
additional terms separately before proceeding.

Thank you for helping us establish whether a legitimate technical path exists.

## Decision after a reply

A support acknowledgement alone is not approval. Continue PUBG-specific work
only after a named technical path and bounded authorized test scope exist.
If no supported path is available, keep general Wine work independent and
retain ordinary play on a supported Windows host as a practical alternative.
No environment hiding, trust spoofing or anti-cheat modification is part of
this proposal. Do not promise that implementing more Windows APIs will obtain
server admission.

This durable source document contains only public technical references and a
generic draft; it introduces no collector or retention change.
