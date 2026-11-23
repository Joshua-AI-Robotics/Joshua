# Joshua Control Panel - React UI

Modern web-based UI for the Joshua robot control system, built with React, TailwindCSS, ShadCN UI, and Vite.

## Tech Stack

- **React 18** - UI framework
- **TypeScript** - Type safety
- **Vite** - Build tool and dev server
- **TailwindCSS** - Utility-first CSS
- **ShadCN UI** - Component library (Radix UI + Tailwind)
- **React Router** - Client-side routing

## Running

From the repo root, build and serve the UI through Docker:

```bash
docker compose --profile production up --build joshua-ui
```

For development with hot reload:

```bash
docker compose -f docker-compose.yml -f docker-compose.dev.yml \
  up zenoh-bridge-ros2dds joshua-ui-dev
```

Open `http://localhost:3000`. See [docs/GETTING_STARTED.md](../docs/GETTING_STARTED.md#web-ui) for Docker host prerequisites and details.

Local host npm is not a supported entrypoint; the UI Dockerfiles install Node dependencies and run Vite/nginx inside containers.

## Operator chat

With `OPENAI_API_KEY` set in the host environment, start the control panel and
its chat backend from the repository root.

```bash
docker compose -f docker-compose.yml -f docker-compose.chat.yml \
  --profile production up --build joshua-ui
```

Open [Chat](http://localhost:3000/chat). The chat override binds the panel to
host loopback. Anyone with access to that panel can send billable requests.
The API key is passed only to the backend container. Regular UI deployments
can continue to run the monitor and config editor without this service.

Decisions selects an answer, clarification, unavailable capability, or node
lookup. Responses writes general answers. A node lookup reads the same Zenoh
graph as Monitor and reports the server's UTC observation time. Set
`ZENOH_BRIDGE_URL` to the bridge address, or to an empty string to disable that
route. Chat 1 has no camera reader or motor command handler.

The backend sends the current turn and up to four recent exchanges to OpenAI,
including node names if a previous reply reported them. A retry of
the latest completed turn returns its recorded reply. Idle conversations expire
after 30 minutes. The 20-second deadline uses local monotonic elapsed time so
network waits stay bounded when robot time pauses. Server logs contain turn and
provider request IDs, routes, outcomes, elapsed time and available token counts.
Message text and session credentials stay out of those logs.

In a Docker dev shell, `bazel test //ui/chat:server_test` exercises the HTTP
handler with local Decisions, Responses and graph fixtures.

## Project Structure

```
ui/
├── src/
│   ├── components/
│   │   ├── ui/          # ShadCN UI components
│   │   └── Layout.tsx   # Main layout component
│   ├── pages/          # Page components
│   │   ├── GeneralPage.tsx
│   │   ├── ConfigPage.tsx
│   │   ├── MonitorPage.tsx
│   │   └── CalibrationPage.tsx
│   ├── lib/            # Utilities
│   │   └── utils.ts    # cn() helper for class merging
│   ├── App.tsx         # Root component with routing
│   ├── main.tsx        # Entry point
│   └── index.css       # Global styles + Tailwind
├── index.html
├── package.json
├── tsconfig.json
├── vite.config.ts
└── tailwind.config.js
```

## Features

- ✅ Modern React 18 with TypeScript
- ✅ TailwindCSS for styling
- ✅ ShadCN UI components
- ✅ React Router for navigation
- ✅ Responsive design
- ✅ Dark mode support (via ShadCN theme)

## Next Steps

1. Implement protobuf-driven config editor
2. Add ROS2 WebSocket integration for monitoring
3. Implement calibration interface
4. Add real-time status updates

## Integration

The Docker build generates protobuf schema from repo protos before building the React app. The production image serves the built static files with nginx.
