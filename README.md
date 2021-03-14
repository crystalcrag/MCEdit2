# MCEdit2
Voxel engine compatible with Minecraft 1.12

Honest try at recreating the creative mode of Minecraft 1.12, using as little dependencies as possible. So far, this are the ones required:
* **OpenGL 4.3**: this is not due to lazyness, but because the terrain is rendered by a couple of draw calls using glMultiDrawArraysIndirect(), which is available only in GLv4.3. Replacing this draw call with one compatible with GLv3 is not going to be easy.
* **SDL1**: why not SDL2 ? Because SDL1 is largely enough for this project.
* **SITGL**: that's a pretty big one. It is used to render/manage user interface (because doing this using only bare bone OpenGL calls is way above my tolerance to pain). SITGL only depends on OpenGL v3.
* **zlib**: needed for reading/writing NBT. Any version can be used.

Why 1.12 ? Because later versions have some significant changes behind the scenes (especially 1.13: the way blocks are stored on disk has changed, not including all the new meachanics introduced by this version). Supporting both is way too much work.

As of now, only Windows is supported, even though there are no major road blocks as long as those dependencies are available on your platform (which probably excludes Mac OS X, due to its poor OpenGL support). SITGL only supports Windows for now and is a fairly large dependency. This code base should be much more portable.


