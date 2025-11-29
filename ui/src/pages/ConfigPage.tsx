import { useState, useCallback, useEffect } from 'react'
import { Button } from '@/components/ui/button'
import {
  Dialog,
  DialogContent,
  DialogHeader,
  DialogTitle,
} from '@/components/ui/dialog'
import { Save, FolderOpen, RotateCcw, Eye, Undo2 } from 'lucide-react'
import { ConfigForm } from '@/components/ConfigForm'
import { loadSchema, type ProtoSchema } from '@/lib/proto-schema'
import { parsePbtxt, formatPbtxt } from '@/lib/pbtxt-parser'

export default function ConfigPage() {
  const [configValue, setConfigValue] = useState<any>({})
  const [schema, setSchema] = useState<ProtoSchema | null>(null)
  const [loading, setLoading] = useState(true)
  const [previewOpen, setPreviewOpen] = useState(false)
  const [previousConfigValue, setPreviousConfigValue] = useState<any>(null)

  useEffect(() => {
    loadSchema()
      .then(setSchema)
      .catch((error) => {
        console.error('Failed to load schema:', error)
      })
      .finally(() => setLoading(false))
  }, [])

  const handleLoad = useCallback(() => {
    const input = document.createElement('input')
    input.type = 'file'
    input.accept = '.pbtxt'
    input.onchange = (e) => {
      const file = (e.target as HTMLInputElement).files?.[0]
      if (!file) return

      const reader = new FileReader()
      reader.onload = (event) => {
        try {
          const content = event.target?.result as string
          const parsed = parsePbtxt(content)
          setConfigValue(parsed)
          setPreviousConfigValue(null) // Clear undo history when loading new file
        } catch (error) {
          console.error('Failed to parse pbtxt file:', error)
          alert('Failed to parse configuration file')
        }
      }
      reader.readAsText(file)
    }
    input.click()
  }, [])

  const handleSave = useCallback(() => {
    try {
      const pbtxtContent = formatPbtxt(configValue)
      const blob = new Blob([pbtxtContent], { type: 'text/plain' })
      const url = URL.createObjectURL(blob)
      const a = document.createElement('a')
      a.href = url
      a.download = 'config.pbtxt'
      document.body.appendChild(a)
      a.click()
      document.body.removeChild(a)
      URL.revokeObjectURL(url)
    } catch (error) {
      console.error('Failed to save pbtxt file:', error)
      alert('Failed to save configuration file')
    }
  }, [configValue])

  const handleClear = useCallback(() => {
    // Save current state before resetting
    setPreviousConfigValue(configValue)
    setConfigValue({})
  }, [configValue])

  const handleUndo = useCallback(() => {
    if (previousConfigValue !== null) {
      setConfigValue(previousConfigValue)
      setPreviousConfigValue(null)
    }
  }, [previousConfigValue])

  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between">
        <div>
          <h2 className="text-3xl font-bold tracking-tight">Configuration</h2>
          <p className="text-muted-foreground">
            Create and edit robot configuration files
          </p>
        </div>
        <div className="flex space-x-2">
          <Button
            variant="outline"
            onClick={() => setPreviewOpen(true)}
          >
            <Eye className="mr-2 h-4 w-4" />
            Preview
          </Button>
          <Button variant="outline" onClick={handleLoad}>
            <FolderOpen className="mr-2 h-4 w-4" />
            Load
          </Button>
          <Button onClick={handleSave}>
            <Save className="mr-2 h-4 w-4" />
            Save
          </Button>
          <Button variant="destructive" onClick={handleClear}>
            <RotateCcw className="mr-2 h-4 w-4" />
            Reset
          </Button>
          {previousConfigValue !== null && (
            <Button variant="outline" onClick={handleUndo}>
              <Undo2 className="mr-2 h-4 w-4" />
              Undo Reset
            </Button>
          )}
        </div>
      </div>

      {loading ? (
        <div className="flex items-center justify-center p-12">
          <p className="text-muted-foreground">Loading schema...</p>
        </div>
      ) : schema ? (
        <ConfigForm
          schema={schema}
          value={configValue}
          onChange={setConfigValue}
        />
      ) : (
        <div className="flex items-center justify-center p-12">
          <p className="text-destructive">Failed to load schema</p>
        </div>
      )}

      <Dialog open={previewOpen} onOpenChange={setPreviewOpen}>
        <DialogContent className="max-w-4xl max-h-[90vh]">
          <DialogHeader>
            <DialogTitle>Configuration Preview</DialogTitle>
          </DialogHeader>
          <div className="mt-4">
            <pre className="whitespace-pre-wrap font-mono text-sm bg-muted p-4 rounded-md overflow-auto max-h-[calc(90vh-150px)]">
              {formatPbtxt(configValue) || '(empty configuration)'}
            </pre>
          </div>
        </DialogContent>
      </Dialog>
    </div>
  )
}

