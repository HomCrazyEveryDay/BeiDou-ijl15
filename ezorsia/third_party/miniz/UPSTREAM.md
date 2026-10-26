miniz 3.0.2, copied from the workspace launcher vendored sources.
Upstream: https://github.com/richgel999/miniz/tree/3.0.2
Upstream files and license are unchanged. Only miniz.c and miniz_tdef.c are compiled;
archive, zlib convenience and inflate APIs are disabled. The audit writer uses raw
DEFLATE in bounded in-memory batches before AES-GCM encryption.
