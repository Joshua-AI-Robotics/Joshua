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
