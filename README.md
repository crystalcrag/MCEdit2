# MCEdit2
Voxel engine compatible with Minecraft 1.12

Honest try at recreating the creative mode of Minecraft 1.12, using as little dependencies as possible. So far, this are the ones required:
* **OpenGL 4.3**: this is not due to lazyness, but because the terrain is rendered by a couple of draw calls using glMultiDrawArraysIndirect(), which is available only in GLv4.3.
* **SDL1**: why not SDL2 ? Because SDL1 is largely enough for this project.
* **SITGL**: that's a pretty big one. It is used to render/manage user interface (because doing this using only OpenGL is way above my tolerance to pain).
* **zlib**: needed for reading/writing NBT.

As of now, only Windows is supported, event though there are no major road blocks as long as those dependencies are available to your platform (which probably excludes Mac OS X, due to its poor support of OpenGL).

