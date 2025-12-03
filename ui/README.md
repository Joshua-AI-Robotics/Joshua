# Joshua Control Panel - React UI

Modern web-based UI for the Joshua robot control system, built with React, TailwindCSS, ShadCN UI, and Vite.

## Tech Stack

- **React 18** - UI framework
- **TypeScript** - Type safety
- **Vite** - Build tool and dev server
- **TailwindCSS** - Utility-first CSS
- **ShadCN UI** - Component library (Radix UI + Tailwind)
- **React Router** - Client-side routing

## Getting Started

### Install Dependencies

```bash
npm install
```

### Development

```bash
npm run dev
```

The app will be available at `http://localhost:3000`

### Build

```bash
npm run build
```

Output will be in the `dist/` directory.

### Preview Production Build

```bash
npm run preview
```

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

1. Implement protobuf-driven config editor (similar to Qt version)
2. Add ROS2 WebSocket integration for monitoring
3. Implement calibration interface
4. Add real-time status updates

## Integration with Bazel

The UI can be integrated into the Bazel build system. The build output (`dist/`) can be served as static files or integrated with the C++ backend.

