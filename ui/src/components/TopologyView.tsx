import { useMemo } from 'react'
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from './ui/card'
import { useROS2Nodes } from '@/hooks/useROS2Nodes'
import { useZenohSSE, type ZenohEvent } from '@/hooks/useZenohSSE'
import TopologyGraph from './TopologyGraph'

interface TopicConnection {
  topic: string
  type: string
  publishers: string[]
  subscribers: string[]
}

interface NodeConnection {
  node: string
  publishes: string[]
  subscribes: string[]
}

export default function TopologyView() {
  const { nodes } = useROS2Nodes()
  const { events: routeEvents } = useZenohSSE('@/*/ros2/route/**')

  const { topics, nodeConnections } = useMemo(() => {
    const topicMap = new Map<string, TopicConnection>()
    const nodeConnMap = new Map<string, NodeConnection>()

    routeEvents.forEach((event: ZenohEvent) => {
      const key = typeof event.key === 'string' ? event.key : event.value?.key || ''
      const value = event.value || {}

      // Parse topic routes: @/{zenoh_id}/ros2/route/topic/pub/{topic} or topic/sub/{topic}
      const topicPubMatch = key.match(/ros2\/route\/topic\/pub\/(.+)$/)
      const topicSubMatch = key.match(/ros2\/route\/topic\/sub\/(.+)$/)

      if (topicPubMatch || topicSubMatch) {
        const topicName = topicPubMatch?.[1] || topicSubMatch?.[1] || ''
        const localNodes = (value.local_nodes || []) as string[]
        const ros2Name = value.ros2_name || topicName
        const ros2Type = value.ros2_type || 'unknown'

        if (!topicMap.has(ros2Name)) {
          topicMap.set(ros2Name, {
            topic: ros2Name,
            type: ros2Type,
            publishers: [],
            subscribers: [],
          })
        }

        const topic = topicMap.get(ros2Name)!

        if (topicPubMatch) {
          // Publisher
          localNodes.forEach((node) => {
            if (!topic.publishers.includes(node)) {
              topic.publishers.push(node)
            }
            // Update node connection
            if (!nodeConnMap.has(node)) {
              nodeConnMap.set(node, { node, publishes: [], subscribes: [] })
            }
            if (!nodeConnMap.get(node)!.publishes.includes(ros2Name)) {
              nodeConnMap.get(node)!.publishes.push(ros2Name)
            }
          })
        } else if (topicSubMatch) {
          // Subscriber
          localNodes.forEach((node) => {
            if (!topic.subscribers.includes(node)) {
              topic.subscribers.push(node)
            }
            // Update node connection
            if (!nodeConnMap.has(node)) {
              nodeConnMap.set(node, { node, publishes: [], subscribes: [] })
            }
            if (!nodeConnMap.get(node)!.subscribes.includes(ros2Name)) {
              nodeConnMap.get(node)!.subscribes.push(ros2Name)
            }
          })
        }
      }
    })

    return {
      topics: Array.from(topicMap.values()),
      nodeConnections: Array.from(nodeConnMap.values()),
    }
  }, [routeEvents])

  return (
    <Card>
      <CardHeader>
        <CardTitle>Topology</CardTitle>
        <CardDescription>
          ROS2 node and topic connections ({nodes.length} nodes, {topics.length} topics)
        </CardDescription>
      </CardHeader>
      <CardContent>
        <div className="space-y-4">
          {/* Graph Visualization */}
          <div>
            <h3 className="text-sm font-semibold mb-2">Graph View</h3>
            <p className="text-xs text-muted-foreground mb-3">
              Blue arrows = publish, Green dashed arrows = subscribe
            </p>
            <TopologyGraph />
          </div>
        </div>
      </CardContent>
    </Card>
  )
}

