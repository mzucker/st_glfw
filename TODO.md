# TODO:

  - see if we can get away with using fewer texture units
  - dotfile api key
  - dotfile default resolution
  - check window scaling at startup to set window size correctly for high DPI
  - cache shadertoy textures
  - option to save downloaded JSON/files to filesystem
  - wrapper script to combine multiple GLSL files + textures into JSON
  - unwrapper script to split JSON into constutient parts (combine with save, above?)
  - command-line options to specify textures?
  - make sure we fail reasonably on missing shadertoy features

## DONE:

  - put title from JSON in window
  - audit null-terminating strings in buffer code
  - use Javascript key codes 
  - debug rendering failures: Shane's quartic, dr2's molecular dynamics
  - replace `code` with `code_file` to point into filesystem for local JSON
  - replace `src` with `src_file` to point into filesystem for local JSON
  - prevent local file access from remote JSON
  - make sure to provide all uniforms that shadertoy does (not gonna do iSampleRate)
