import { useMemo } from 'react'
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from './ui/card'
import { useZenohSSE, type ZenohEvent } from '@/hooks/useZenohSSE'

interface Topic {
  name: string
  type: string
  publishers: string[]
  subscribers: string[]
}

export default function TopicsList() {
  const { events: routeEvents, connected } = useZenohSSE('@/*/ros2/route/**')

  const topics = useMemo(() => {
    const topicMap = new Map<string, Topic>()

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
            name: ros2Name,
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
          })
        } else if (topicSubMatch) {
          // Subscriber
          localNodes.forEach((node) => {
            if (!topic.subscribers.includes(node)) {
              topic.subscribers.push(node)
            }
          })
        }
      }
    })

    // Sort by namespace first, then by name
    return Array.from(topicMap.values()).sort((a, b) => {
      // Extract namespace and name from topic name
      // Topic names can be like "/robot/status" (namespace: "/robot", name: "status")
      // or "chatter" (namespace: "/", name: "chatter")
      const parseTopic = (topicName: string) => {
        const parts = topicName.split('/').filter((p) => p)
        if (parts.length > 1) {
          return {
            namespace: '/' + parts.slice(0, -1).join('/'),
            name: parts[parts.length - 1],
          }
        }
        return { namespace: '/', name: topicName }
      }

      const aParsed = parseTopic(a.name)
      const bParsed = parseTopic(b.name)

      // First sort by namespace
      const namespaceCompare = aParsed.namespace.localeCompare(bParsed.namespace)
      if (namespaceCompare !== 0) {
        return namespaceCompare
      }
      // If namespaces are equal, sort by name
      return aParsed.name.localeCompare(bParsed.name)
    })
  }, [routeEvents])

  return (
    <Card>
      <CardHeader>
        <CardTitle>Topics</CardTitle>
        <CardDescription>
          ROS2 topics and their connections ({topics.length} topics)
        </CardDescription>
      </CardHeader>
      <CardContent>
        {topics.length === 0 ? (
          <div className="rounded-lg border border-dashed p-12 text-center">
            <p className="text-muted-foreground">
              {connected ? 'No topics available' : 'Waiting for connection...'}
            </p>
          </div>
        ) : (
          <div className="space-y-3">
            {topics.map((topic) => (
              <div
                key={topic.name}
                className="rounded-lg border p-4 bg-card hover:bg-accent/50 transition-colors"
              >
                <div className="flex items-center justify-between mb-3">
                  <div className="font-mono font-semibold text-sm">{topic.name}</div>
                  <span className="text-xs text-muted-foreground px-2 py-1 rounded bg-muted">
                    {topic.type}
                  </span>
                </div>
                <div className="grid grid-cols-2 gap-4 text-xs">
                  <div>
                    <div className="text-muted-foreground mb-2 font-medium">Publishers:</div>
                    {topic.publishers.length > 0 ? (
                      <div className="space-y-1">
                        {topic.publishers.map((node) => (
                          <div
                            key={node}
                            className="px-2 py-1 rounded bg-blue-100 dark:bg-blue-900/30 text-blue-700 dark:text-blue-300 inline-block mr-1 mb-1"
                          >
                            {node}
                          </div>
                        ))}
                      </div>
                    ) : (
                      <span className="text-muted-foreground italic">None</span>
                    )}
                  </div>
                  <div>
                    <div className="text-muted-foreground mb-2 font-medium">Subscribers:</div>
                    {topic.subscribers.length > 0 ? (
                      <div className="space-y-1">
                        {topic.subscribers.map((node) => (
                          <div
                            key={node}
                            className="px-2 py-1 rounded bg-green-100 dark:bg-green-900/30 text-green-700 dark:text-green-300 inline-block mr-1 mb-1"
                          >
                            {node}
                          </div>
                        ))}
                      </div>
                    ) : (
                      <span className="text-muted-foreground italic">None</span>
                    )}
                  </div>
                </div>
              </div>
            ))}
          </div>
        )}
      </CardContent>
    </Card>
  )
}

