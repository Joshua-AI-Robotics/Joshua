import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '@/components/ui/card'
import { Tabs, TabsList, TabsTrigger, TabsContent } from '@/components/ui/tabs'
import { useROS2Nodes } from '@/hooks/useROS2Nodes'
import { useZenohSSE } from '@/hooks/useZenohSSE'
import { AlertCircle, CheckCircle2 } from 'lucide-react'
import TopologyGraph from '@/components/TopologyGraph'
import NodesList from '@/components/NodesList'
import TopicsList from '@/components/TopicsList'
import LogViewer from '@/components/LogViewer'

export default function MonitorPage() {
  const { error: nodesError, connected: nodesConnected } = useROS2Nodes()
  const { error: routesError, connected: routesConnected } = useZenohSSE('@/*/ros2/route/**')

  const connectionStatus = nodesConnected && routesConnected
  const hasError = nodesError || routesError

  return (
    <div className="space-y-6">
      <div>
        <h2 className="text-3xl font-bold tracking-tight">Monitor</h2>
        <p className="text-muted-foreground">
          Monitor ROS2 nodes and topics via Zenoh bridge
        </p>
      </div>

      {/* Connection Status */}
      <Card>
        <CardHeader>
          <CardTitle>Connection Status</CardTitle>
          <CardDescription>Zenoh bridge REST API connection</CardDescription>
        </CardHeader>
        <CardContent>
          <div className="flex items-center space-x-2">
            {connectionStatus ? (
              <>
                <CheckCircle2 className="h-5 w-5 text-green-600" />
                <span className="text-sm font-medium text-green-600">Connected</span>
                <span className="text-sm text-muted-foreground">(http://localhost:8000)</span>
              </>
            ) : (
              <>
                <AlertCircle className="h-5 w-5 text-yellow-600" />
                <span className="text-sm font-medium text-yellow-600">Connecting...</span>
                {hasError && (
                  <span className="text-sm text-muted-foreground">
                    {(nodesError || routesError)?.message}
                  </span>
                )}
              </>
            )}
          </div>
        </CardContent>
      </Card>

      {/* Main Tabs */}
      <Tabs defaultValue="nodes-topics" className="w-full">
        <TabsList>
          <TabsTrigger value="nodes-topics">Nodes and Topics</TabsTrigger>
          <TabsTrigger value="logs">Logs</TabsTrigger>
        </TabsList>

        <TabsContent value="nodes-topics" className="space-y-6 mt-6">
          {/* Graph Topology */}
          <Card>
            <CardHeader>
              <CardTitle>Graph Topology</CardTitle>
              <CardDescription>
                Visual representation of ROS2 node and topic connections
              </CardDescription>
            </CardHeader>
            <CardContent>
              <div className="space-y-2">
                <p className="text-xs text-muted-foreground">
                  Blue arrows = publish, Green dashed arrows = subscribe
                </p>
                <TopologyGraph />
              </div>
            </CardContent>
          </Card>

          {/* Nodes */}
          <NodesList />

          {/* Topics */}
          <TopicsList />
        </TabsContent>

        <TabsContent value="logs" className="mt-6">
          <LogViewer />
        </TabsContent>
      </Tabs>
    </div>
  )
}

