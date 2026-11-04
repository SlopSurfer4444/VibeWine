# Sanitized recorded evidence

JSON derivatives preserve recorded source/artifact hashes and check results while
replacing private workspace and temporary paths and omitting raw command output fields. Their original archive member
hashes and public hashes are in WAVE7_ADAPTATIONS.json. They are not executable
recipes and do not constitute new tests of this public layout. Signed/unsigned
artifact identities differ; the bridge unsigned hash is receipt-bound because
signing replaced its original output in place. Raw logs are excluded.
