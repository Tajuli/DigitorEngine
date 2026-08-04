# Security checklist

- verify publisher signature before extraction
- verify archive SHA-256 before staging
- reject revoked packages and publishers
- reject absolute, parent-traversal and drive-prefixed paths
- reject duplicate entries, symbolic links and executable payloads by default
- enforce archive, entry, expanded-size and expansion-ratio limits
- extract only into a staging directory
- activate atomically
- roll back failed activation
- never execute downloaded native libraries
