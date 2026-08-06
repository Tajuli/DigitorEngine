# Error policy

Invalid metadata, unsupported format, memory limit, cancellation, codec failure, GPU upload failure, device loss and encode failure are returned to the caller. A GPU-selected session does not retry processing on CPU. Partial output is not published after cancellation or failure.
