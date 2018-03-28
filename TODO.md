# TODO:

  - option to save downloaded JSON/files to filesystem
  - unwrapper script to split JSON into constutient parts (combine with save, above?)
  - cache shadertoy textures
  - dotfile api key
  - dotfile default resolution
  - check window scaling at startup to set window size correctly for high DPI
  - wrapper script to combine multiple GLSL files + textures into JSON
  - command-line options to specify textures?

## DONE:

  - make sure we fail reasonably on missing shadertoy features 
  - make sure to provide all uniforms that shadertoy does 
  - we really only ever needed 4 texture units
  - put title from JSON in window
  - audit null-terminating strings in buffer code
  - use Javascript key codes 
  - debug rendering failures: Shane's quartic, dr2's molecular dynamics
  - replace `code` with `code_file` to point into filesystem for local JSON
  - replace `src` with `src_file` to point into filesystem for local JSON
  - prevent local file access from remote JSON
