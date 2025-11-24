import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Play, Square, RefreshCw } from 'lucide-react'

export default function GeneralPage() {
  return (
    <div className="space-y-6">
      <div>
        <h2 className="text-3xl font-bold tracking-tight">General</h2>
        <p className="text-muted-foreground">
          System status and control
        </p>
      </div>

      <div className="grid gap-4 md:grid-cols-2 lg:grid-cols-3">
        <Card>
          <CardHeader>
            <CardTitle>System Status</CardTitle>
            <CardDescription>Current system state</CardDescription>
          </CardHeader>
          <CardContent>
            <div className="space-y-2">
              <div className="flex items-center justify-between">
                <span className="text-sm text-muted-foreground">Status</span>
                <span className="text-sm font-medium text-green-600">Ready</span>
              </div>
              <div className="flex items-center justify-between">
                <span className="text-sm text-muted-foreground">Nodes Running</span>
                <span className="text-sm font-medium">0</span>
              </div>
            </div>
          </CardContent>
        </Card>

        <Card>
          <CardHeader>
            <CardTitle>Quick Actions</CardTitle>
            <CardDescription>Control robot operations</CardDescription>
          </CardHeader>
          <CardContent className="space-y-2">
            <Button className="w-full" size="sm">
              <Play className="mr-2 h-4 w-4" />
              Start
            </Button>
            <Button className="w-full" variant="destructive" size="sm">
              <Square className="mr-2 h-4 w-4" />
              Stop
            </Button>
            <Button className="w-full" variant="outline" size="sm">
              <RefreshCw className="mr-2 h-4 w-4" />
              Restart
            </Button>
          </CardContent>
        </Card>

        <Card>
          <CardHeader>
            <CardTitle>Configuration</CardTitle>
            <CardDescription>Current configuration file</CardDescription>
          </CardHeader>
          <CardContent>
            <div className="space-y-2">
              <p className="text-sm text-muted-foreground">No config loaded</p>
              <Button variant="outline" size="sm" className="w-full">
                Load Config
              </Button>
            </div>
          </CardContent>
        </Card>
      </div>
    </div>
  )
}

