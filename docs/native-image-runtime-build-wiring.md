# Platform build wiring

- Windows builds compile the WIC codec through the established native still-image media translation unit.
- Android builds compile the AImageDecoder adapter through the same media unit and provide encode through the app/plugin callback.
- Apple builds compile ImageIO as Objective-C++ beside the existing Apple native media bindings; the provider registers its Metal upload/readback callbacks before opening a session.

The portable build never compiles Objective-C++ or Android NDK calls, and unsupported platform factories return unavailable rather than changing backend.
