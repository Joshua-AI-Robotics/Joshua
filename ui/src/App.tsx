import { BrowserRouter, Routes, Route, Navigate } from 'react-router-dom'
import Layout from './components/Layout'
import ConfigPage from './pages/ConfigPage'
import MonitorPage from './pages/MonitorPage'

function App() {
  return (
    <BrowserRouter>
      <Layout>
        <Routes>
          <Route path="/" element={<Navigate to="/monitor" replace />} />
          <Route path="/config" element={<ConfigPage />} />
          <Route path="/monitor" element={<MonitorPage />} />
        </Routes>
      </Layout>
    </BrowserRouter>
  )
}

export default App

