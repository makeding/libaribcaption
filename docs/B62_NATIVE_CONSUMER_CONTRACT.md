# ARIB STD-B62 native consumer contract

This fork exposes one lossless document-oriented path for native players. A
consumer decodes a complete ARIB-TTML document with
`aribcc_b62_decoder_decode_document_with_resources()` and appends the returned
`aribcc_b62_document_result_t` to the renderer as one operation. It must not
flatten the result into one subtitle event or append the captions separately,
because the document sidecar owns the ruby associations, WOFF/SVG fonts,
background images, audio metadata, and scoped resource bytes needed later at
render time.

The renderer copies the complete document result before the append call
returns. The caller remains responsible for calling
`aribcc_b62_document_result_cleanup()` after either a successful or failed
append. Appending is atomic: an invalid caption rejects the whole document and
must not leave a partial timeline in the renderer.

The renderer produces RGBA bitmaps for every active text cue and
PNG/SVG background image at the requested PTS. Embedded `smpte:image` resources
and `subt://n` resources follow the same timing, layout, scaling, and z-order
rules. Background images are below text and ruby; document resources remain
alive until their last stored caption or image is flushed or evicted.

`arib-tt:audio` is preserved in the sidecar with its resource, timing, and loop
metadata. This library does not mix or play that audio.

The native transport accepts only uncompressed ARIB-TTML
(`compression_type=0`). EXI compression types 1 and 2 are rejected by the
transporting consumer with an explicit unsupported error before this XML
decoder is called; they must never be treated as empty subtitles.

On seek, track switch, service discontinuity, or stream reset, the consumer
must reset the B62 decoder and flush the renderer together. A non-zero resource
scope identifies one subtitle-stream MPU sequence. Resources from an older
scope must not resolve after a scope change. A zero scope is call-local and is
never retained by the decoder.

Native acceptance covers multiple cues from one document, clear documents,
live continuation, ruby, WOFF and SVG fonts, PNG and SVG background images,
caption-plane size changes, resource-scope changes, decoder reset, and renderer
flush. Both the C and C++ APIs and the no-exceptions build must pass.
