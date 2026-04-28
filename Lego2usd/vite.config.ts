import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import fs from 'node:fs'
import path from 'node:path'

// Vite's SPA fallback returns index.html (200) for any missing file, which
// breaks LDrawLoader's "try parts/ then p/" probing — it gets HTML back on
// the first attempt and never retries. This middleware makes missing files
// under /ldraw/ return a real 404 so the loader can continue its search.
function ldrawStrict404() {
  return {
    name: 'ldraw-strict-404',
    configureServer(server: { middlewares: { use: (path: string, handler: (req: { url?: string }, res: { statusCode: number; end: (msg: string) => void }, next: () => void) => void) => void } }) {
      const publicDir = path.resolve(__dirname, 'public')
      server.middlewares.use('/ldraw', (req, res, next) => {
        const url = req.url ?? ''
        const relative = url.split('?')[0]
        const filePath = path.join(publicDir, 'ldraw', relative)
        if (!filePath.startsWith(path.join(publicDir, 'ldraw'))) {
          res.statusCode = 403
          res.end('forbidden')
          return
        }
        if (!fs.existsSync(filePath) || !fs.statSync(filePath).isFile()) {
          res.statusCode = 404
          res.end('not found')
          return
        }
        next()
      })
    },
  }
}

export default defineConfig({
  plugins: [react(), ldrawStrict404()],
  resolve: {
    dedupe: ['three'],
  },
})
