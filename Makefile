default:
		gcc tty_renderer.c -o tty_renderer -levdev -lEGL -lGLESv2 -lgbm -lm
