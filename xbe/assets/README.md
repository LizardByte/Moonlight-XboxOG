SDL_image does not accept "text" parameters in SVG images, due to a limitation of nanosvg.
We must convert images so they do not contain any text parameters.
