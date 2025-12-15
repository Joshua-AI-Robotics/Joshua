import { ReactNode } from 'react'
import { Link, useLocation } from 'react-router-dom'
import { cn } from '@/lib/utils'

interface LayoutProps {
  children: ReactNode
}

const navigation = [
  { name: 'General', href: '/', icon: '🏠' },
  { name: 'Monitor', href: '/monitor', icon: '📊' },
  { name: 'Config', href: '/config', icon: '⚙️' },
  { name: 'Calibration', href: '/calibration', icon: '🔧' },
]

export default function Layout({ children }: LayoutProps) {
  const location = useLocation()

  return (
    <div className="min-h-screen bg-background">
      {/* Header */}
      <header className="border-b">
        <div className="container mx-auto px-4 py-4">
          <div className="flex items-center justify-between">
            <div className="flex items-center space-x-4">
              <h1 className="text-2xl font-bold">Joshua Control Panel</h1>
              <span className="text-sm text-muted-foreground">v0.1.0</span>
            </div>
          </div>
        </div>
      </header>

      {/* Navigation */}
      <nav className="border-b bg-muted/40">
        <div className="container mx-auto px-4">
          <div className="flex space-x-1">
            {navigation.map((item) => {
              const isActive = location.pathname === item.href
              return (
                <Link
                  key={item.name}
                  to={item.href}
                  className={cn(
                    'px-4 py-3 text-sm font-medium transition-colors',
                    'hover:bg-background hover:text-foreground',
                    isActive
                      ? 'border-b-2 border-primary bg-background text-foreground'
                      : 'text-muted-foreground'
                  )}
                >
                  <span className="mr-2">{item.icon}</span>
                  {item.name}
                </Link>
              )
            })}
          </div>
        </div>
      </nav>

      {/* Main Content */}
      <main className="container mx-auto px-4 py-6">
        {children}
      </main>
    </div>
  )
}

