# Memory policy

Decoded width, height, row stride and byte count are validated before processing. Orientation and export transforms use deterministic row ranges through the shared CPU executor. GPU providers obey the configured tile dimensions and maximum decoded bytes; allocation failure returns out-of-memory without publishing partial output.
