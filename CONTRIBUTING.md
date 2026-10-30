
# CONTRIBUTING

Some basic guidelines for contributing to this project

- PRs should target the 1181dev branch or their own branch
- Target 1.18.1 (7272) from March of 2026
- Any SQL updates should be placed in sql/wip_updates/ in a SQL script named for its table. They will be merged into a proper full update when deemed appropriate (Mostly when a spec is fully functional)
- Always comment SQL blocks
- Test all contributions thoroughly before issuing a PR. ([DPSMate][21] is a great tool to really test changes)
- Always point to issues that you are resolving in PRs. If no issue exists, create one and label appropriately.
- Avoid direct spell edits, instead use spell_affect, spell_proc_event, spell_scripts, or backend code when needed. In rare cases spell_mod can be used to edit the spell on the fly

# Behavioral Changes
Any modification of creature, script, quest, etc, behavior should be accompanied by at least video proof taken from a real turtle source server (See [Valid Data Sources](docs/development/valid-data-sources.md))  
Ideally included would be a video, anonymized pcap, and combat log, but the latter two are not always needed depending on the behavior that is being modified

[21]: https://github.com/laytya/DPSMate

