import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '@/components/ui/card'

export default function MonitorPage() {
  return (
    <div className="space-y-6">
      <div>
        <h2 className="text-3xl font-bold tracking-tight">Monitor</h2>
        <p className="text-muted-foreground">
          Monitor ROS2 nodes and topics
        </p>
      </div>

      <Card>
        <CardHeader>
          <CardTitle>Active Nodes</CardTitle>
          <CardDescription>Currently running ROS2 nodes</CardDescription>
        </CardHeader>
        <CardContent>
          <div className="rounded-lg border border-dashed p-12 text-center">
            <p className="text-muted-foreground">No nodes running</p>
          </div>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle>Topics</CardTitle>
          <CardDescription>Active ROS2 topics</CardDescription>
        </CardHeader>
        <CardContent>
          <div className="rounded-lg border border-dashed p-12 text-center">
            <p className="text-muted-foreground">No topics available</p>
          </div>
        </CardContent>
      </Card>
    </div>
  )
}

