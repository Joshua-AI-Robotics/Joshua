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
- ✅ Protobuf-schema-driven configuration forms
- ✅ Load, edit, preview, and save `.pbtxt` configuration files
- ✅ ROS 2 node, topic, and topology views plus Zenoh bridge admin records

## Boundaries

The config editor creates and edits files in the browser. It does not run the
repository's semantic config validation, launch Joshua, or prove that a preset
is safe to use with connected hardware. Follow the [config
guide](../config/README.md) and [hardware-safety rules](../AGENTS.md) before
running a saved preset.

The monitoring view reads ROS 2 graph metadata from the Zenoh bridge admin
space. The Logs tab formats records returned by that admin API; it is not a
`/rosout` log stream. A connected dashboard does not by itself verify robot or
hardware behavior.

## Integration

The Docker build generates protobuf schema from repo protos before building the React app. The production image serves the built static files with nginx.
