import { useMemo } from 'react'
import { useROS2Nodes } from '@/hooks/useROS2Nodes'
import { useZenohSSE, type ZenohEvent } from '@/hooks/useZenohSSE'

interface GraphNode {
  id: string
  label: string
  type: 'node' | 'topic'
  x: number
  y: number
  connections: string[]
}

interface GraphEdge {
  from: string
  to: string
  type: 'publish' | 'subscribe'
}

export default function TopologyGraph() {
  const { nodes } = useROS2Nodes()
  const { events: routeEvents } = useZenohSSE('@/*/ros2/route/**')

  const { graphNodes, graphEdges } = useMemo(() => {
    const nodeMap = new Map<string, GraphNode>()
    const topicMap = new Map<string, GraphNode>()
    const edges: GraphEdge[] = []

    // Create nodes for ROS2 nodes
    nodes.forEach((node) => {
      const nodeId = `node_${node.name}`
      nodeMap.set(nodeId, {
        id: nodeId,
        label: node.name,
        type: 'node',
        x: 0, // Will be calculated
        y: 0, // Will be calculated
        connections: [],
      })
    })

    // Parse route events to build graph
    routeEvents.forEach((event: ZenohEvent) => {
      const key = typeof event.key === 'string' ? event.key : event.value?.key || ''
      const value = event.value || {}

      const topicPubMatch = key.match(/ros2\/route\/topic\/pub\/(.+)$/)
      const topicSubMatch = key.match(/ros2\/route\/topic\/sub\/(.+)$/)

      if (topicPubMatch || topicSubMatch) {
        const topicName = topicPubMatch?.[1] || topicSubMatch?.[1] || ''
        const ros2Name = value.ros2_name || topicName
        const localNodes = (value.local_nodes || []) as string[]

        // Create topic node if it doesn't exist
        if (!topicMap.has(ros2Name)) {
          topicMap.set(ros2Name, {
            id: `topic_${ros2Name}`,
            label: ros2Name,
            type: 'topic',
            x: 0,
            y: 0,
            connections: [],
          })
        }

        if (topicPubMatch) {
          // Publisher connections
          localNodes.forEach((nodeName) => {
            const nodeId = `node_${nodeName.replace(/^\//, '')}`
            if (nodeMap.has(nodeId)) {
              edges.push({
                from: nodeId,
                to: `topic_${ros2Name}`,
                type: 'publish',
              })
            }
          })
        } else if (topicSubMatch) {
          // Subscriber connections
          localNodes.forEach((nodeName) => {
            const nodeId = `node_${nodeName.replace(/^\//, '')}`
            if (nodeMap.has(nodeId)) {
              edges.push({
                from: `topic_${ros2Name}`,
                to: nodeId,
                type: 'subscribe',
              })
            }
          })
        }
      }
    })

    // Stable layout: use deterministic positioning based on node/topic names
    // This ensures positions don't change when nodes are added/removed
    const allNodes = Array.from(nodeMap.values()).concat(Array.from(topicMap.values()))
    
    // Sort nodes and topics by name for consistent ordering
    const sortedNodes = Array.from(nodeMap.values()).sort((a, b) => a.label.localeCompare(b.label))
    const sortedTopics = Array.from(topicMap.values()).sort((a, b) => a.label.localeCompare(b.label))
    
    const centerX = 600
    const centerY = 400
    const nodeRadius = 500
    const topicRadius = 300

    // Create a stable position map based on node/topic names
    const positionMap = new Map<string, { x: number; y: number }>()
    
    // Position nodes in a circle - use sorted order for stability
    sortedNodes.forEach((node, idx) => {
      const nodeId = node.id
      if (!positionMap.has(nodeId)) {
        const angle = (idx * 2 * Math.PI) / Math.max(sortedNodes.length, 1)
        positionMap.set(nodeId, {
          x: centerX + nodeRadius * Math.cos(angle),
          y: centerY + nodeRadius * Math.sin(angle),
        })
      }
    })
    
    // Position topics in inner circle - use sorted order for stability
    sortedTopics.forEach((topic, idx) => {
      const topicId = topic.id
      if (!positionMap.has(topicId)) {
        const angle = (idx * 2 * Math.PI) / Math.max(sortedTopics.length, 1)
        positionMap.set(topicId, {
          x: centerX + topicRadius * Math.cos(angle),
          y: centerY + topicRadius * Math.sin(angle),
        })
      }
    })
    
    // Apply stable positions to all nodes
    allNodes.forEach((node) => {
      const pos = positionMap.get(node.id)
      if (pos) {
        node.x = pos.x
        node.y = pos.y
      } else {
        // Fallback positioning (shouldn't happen)
        node.x = centerX
        node.y = centerY
      }
    })

    return {
      graphNodes: allNodes,
      graphEdges: edges,
    }
  }, [nodes, routeEvents])

  if (graphNodes.length === 0) {
    return (
      <div className="flex items-center justify-center h-[800px] border rounded-lg bg-muted/20">
        <p className="text-muted-foreground">No topology data available</p>
      </div>
    )
  }

  // Calculate dynamic viewBox based on node positions with padding
  const allX = graphNodes.map((n) => n.x)
  const allY = graphNodes.map((n) => n.y)
  const padding = 150
  const minX = Math.min(...allX, 0) - padding
  const maxX = Math.max(...allX, 1200) + padding
  const minY = Math.min(...allY, 0) - padding
  const maxY = Math.max(...allY, 800) + padding
  const viewBoxWidth = Math.max(maxX - minX, 1400)
  const viewBoxHeight = Math.max(maxY - minY, 1000)

  return (
    <div className="w-full h-[800px] border rounded-lg bg-muted/20 overflow-auto">
      <svg
        width="100%"
        height="100%"
        viewBox={`${minX} ${minY} ${viewBoxWidth} ${viewBoxHeight}`}
        className="bg-background"
        style={{ minWidth: `${viewBoxWidth}px`, minHeight: `${viewBoxHeight}px` }}
        preserveAspectRatio="xMidYMid meet"
      >
        {/* Draw edges */}
        {graphEdges.map((edge, idx) => {
          const fromNode = graphNodes.find((n) => n.id === edge.from)
          const toNode = graphNodes.find((n) => n.id === edge.to)
          if (!fromNode || !toNode) return null

          const isPublish = edge.type === 'publish'
          const color = isPublish ? '#3b82f6' : '#10b981' // blue for publish, green for subscribe
          const strokeDasharray = isPublish ? 'none' : '5,5'

          return (
            <line
              key={`edge-${idx}`}
              x1={fromNode.x}
              y1={fromNode.y}
              x2={toNode.x}
              y2={toNode.y}
              stroke={color}
              strokeWidth={3}
              strokeDasharray={strokeDasharray}
              opacity={0.7}
              markerEnd={isPublish ? 'url(#arrowhead-blue)' : 'url(#arrowhead-green)'}
            />
          )
        })}

        {/* Arrow markers */}
        <defs>
          <marker
            id="arrowhead-blue"
            markerWidth="10"
            markerHeight="10"
            refX="9"
            refY="3"
            orient="auto"
          >
            <polygon points="0 0, 10 3, 0 6" fill="#3b82f6" />
          </marker>
          <marker
            id="arrowhead-green"
            markerWidth="10"
            markerHeight="10"
            refX="9"
            refY="3"
            orient="auto"
          >
            <polygon points="0 0, 10 3, 0 6" fill="#10b981" />
          </marker>
        </defs>

        {/* Draw nodes */}
        {graphNodes.map((node) => {
          const isNode = node.type === 'node'
          const fill = isNode ? '#6366f1' : '#8b5cf6' // indigo for nodes, purple for topics
          const radius = isNode ? 40 : 30

          return (
            <g key={node.id}>
              <circle
                cx={node.x}
                cy={node.y}
                r={radius}
                fill={fill}
                stroke="white"
                strokeWidth={2}
                className="cursor-pointer hover:opacity-80 transition-opacity"
              />
              <text
                x={node.x}
                y={node.y + radius + 20}
                textAnchor="middle"
                className="fill-foreground font-mono"
                style={{ fontSize: '12px', fontWeight: '500' }}
              >
                {node.label.length > 20 ? node.label.slice(0, 20) + '...' : node.label}
              </text>
              {/* Node type indicator */}
              <text
                x={node.x}
                y={node.y}
                textAnchor="middle"
                dominantBaseline="middle"
                className="fill-white font-bold"
                style={{ fontSize: isNode ? '16px' : '14px' }}
              >
                {isNode ? 'N' : 'T'}
              </text>
            </g>
          )
        })}
      </svg>
    </div>
  )
}

