import { useMemo } from 'react'
import { useZenohSSE, type ZenohEvent } from './useZenohSSE'

export interface ROS2Node {
  name: string
  namespace: string
  lastSeen: number
  heartbeat: number
}

/**
 * Hook to subscribe to ROS2 nodes via Zenoh bridge
 * Parses and deduplicates node information from SSE stream
 */
export function useROS2Nodes(bridgeUrl?: string) {
  // Use wildcard to match any Zenoh ID: @/*/ros2/node/**
  // The bridge stores nodes under @/{zenoh_id}/ros2/node/... not @/local/...
  const { events, error, connected } = useZenohSSE('@/*/ros2/node/**', {
    bridgeUrl,
    enabled: true,
  })

  const nodes = useMemo(() => {
    const nodeMap = new Map<string, ROS2Node>()

    console.log('Parsing ROS2 nodes from events:', { eventCount: events.length })

    events.forEach((event: ZenohEvent) => {
      try {
        // Parse the Zenoh key to extract node information
        // Format: @/{zenoh_id}/ros2/node/{gid}/{node_name}
        // Example: @/e6ed7de38d04131a78acd4067ab2e26f/ros2/node/010f6d761800000e00000000000001c1/listener
        const key = typeof event.key === 'string' ? event.key : event.value?.key || ''
        // Match pattern: @/{zenoh_id}/ros2/node/{gid}/{node_name}
        const match = key.match(/@\/[^/]+\/ros2\/node\/[^/]+\/(.+)$/)
        
        if (match) {
          const nodeName = match[1]
          // Extract namespace from node name (e.g., "/namespace/node" or just "node")
          const nameParts = nodeName.split('/').filter((p: string) => p)
          const actualNodeName = nameParts.length > 1 ? nameParts.slice(-1)[0] : nodeName
          const namespace = nameParts.length > 1 ? '/' + nameParts.slice(0, -1).join('/') : '/'
          
          nodeMap.set(nodeName, {
            name: actualNodeName,
            namespace: namespace,
            lastSeen: event.timestamp,
            heartbeat: event.timestamp,
          })
          console.log('Parsed node:', { key, nodeName, actualNodeName, namespace })
        } else {
          // Fallback: try to parse from value or extract from key
          const value = event.value
          if (value && typeof value === 'object') {
            // Try to extract node name from key as last resort
            const nodeName = key.split('/').pop() || 'unknown'
            nodeMap.set(nodeName, {
              name: nodeName,
              namespace: '/',
              lastSeen: event.timestamp,
              heartbeat: event.timestamp,
            })
            console.log('Parsed node (fallback):', { key, nodeName })
          } else {
            console.warn('Failed to parse node - no match and no value:', { key, value: event.value })
          }
        }
      } catch (err) {
        console.warn('Failed to parse ROS2 node event:', err, event)
      }
    })

    // Sort by namespace first, then by name
    const nodeArray = Array.from(nodeMap.values()).sort((a, b) => {
      // First sort by namespace
      const namespaceCompare = a.namespace.localeCompare(b.namespace)
      if (namespaceCompare !== 0) {
        return namespaceCompare
      }
      // If namespaces are equal, sort by name
      return a.name.localeCompare(b.name)
    })
    console.log('Parsed nodes result:', { count: nodeArray.length, nodes: nodeArray })
    return nodeArray
  }, [events])

  return { nodes, error, connected }
}

