import { Card, CardContent, CardDescription, CardHeader, CardTitle } from './ui/card'
import { useROS2Nodes } from '@/hooks/useROS2Nodes'
import { Clock } from 'lucide-react'

function formatTimeAgo(timestamp: number): string {
  const seconds = Math.floor((Date.now() - timestamp) / 1000)
  if (seconds < 60) return `${seconds}s ago`
  const minutes = Math.floor(seconds / 60)
  if (minutes < 60) return `${minutes}m ago`
  const hours = Math.floor(minutes / 60)
  return `${hours}h ago`
}

export default function NodesList() {
  const { nodes, connected } = useROS2Nodes()

  return (
    <Card>
      <CardHeader>
        <CardTitle>Nodes</CardTitle>
        <CardDescription>
          Currently running ROS2 nodes ({nodes.length} found)
        </CardDescription>
      </CardHeader>
      <CardContent>
        {nodes.length === 0 ? (
          <div className="rounded-lg border border-dashed p-12 text-center">
            <p className="text-muted-foreground">
              {connected ? 'No nodes running' : 'Waiting for connection...'}
            </p>
          </div>
        ) : (
          <div className="space-y-2">
            {nodes.map((node) => (
              <div
                key={`${node.namespace}/${node.name}`}
                className="flex items-center justify-between rounded-lg border p-4 hover:bg-accent transition-colors"
              >
                <div className="flex-1">
                  <div className="flex items-center space-x-2">
                    <span className="font-medium font-mono">
                      {node.namespace === '/' ? node.name : `${node.namespace}/${node.name}`}
                    </span>
                  </div>
                  <div className="mt-1 flex items-center space-x-4 text-sm text-muted-foreground">
                    <span>Namespace: {node.namespace}</span>
                  </div>
                </div>
                <div className="flex items-center space-x-2 text-sm text-muted-foreground">
                  <Clock className="h-4 w-4" />
                  <span>{formatTimeAgo(node.lastSeen)}</span>
                </div>
              </div>
            ))}
          </div>
        )}
      </CardContent>
    </Card>
  )
}

