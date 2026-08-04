# Project Agent Notes

## Windows patch-tool failure

- In this workspace, `apply_patch` may fail before reading a file with a Windows sandbox refresh error such as `windows sandbox failed: helper_unknown_error: setup refresh had errors`.
- Always try `apply_patch` first for source edits. If it fails with that specific sandbox-refresh error, do not keep retrying the same patch.
- Use a narrowly scoped PowerShell/.NET exact-text replacement as the fallback. The fallback must verify that every expected source fragment exists before writing, preserve unrelated user changes, write UTF-8 without a BOM, and normalize edited source files to LF.
- After any fallback edit, inspect `git diff`, run `git diff --check`, and compile or test the affected code before reporting completion.
- Do not treat ordinary patch-context mismatches as this sandbox failure; re-read the file and correct the patch instead.
