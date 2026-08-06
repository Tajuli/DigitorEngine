# Formats

JPEG, PNG and WebP are represented by the common codec contract. PNG/WebP preserve alpha; JPEG requires flattening. EXIF orientation and color metadata identity are carried into the shared processing session. Actual codec availability is reported by the selected platform adapter and is never emulated by another platform codec.
