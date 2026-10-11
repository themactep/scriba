import { defineConfig } from 'vite'

export default defineConfig({
  // Relative asset URLs so the build works both at the project Pages path
  // (/scriba/) and at the custom-domain root (scriba.thingino.com/).
  base: './',
  build: {
    outDir: 'dist',
    emptyOutDir: true,
  },
})
