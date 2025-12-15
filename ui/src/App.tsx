import { BrowserRouter, Routes, Route } from 'react-router-dom'
import Layout from './components/Layout'
import ConfigPage from './pages/ConfigPage'
import MonitorPage from './pages/MonitorPage'
import GeneralPage from './pages/GeneralPage'
import CalibrationPage from './pages/CalibrationPage'

function App() {
  return (
    <BrowserRouter>
      <Layout>
        <Routes>
          <Route path="/" element={<GeneralPage />} />
          <Route path="/config" element={<ConfigPage />} />
          <Route path="/monitor" element={<MonitorPage />} />
          <Route path="/calibration" element={<CalibrationPage />} />
        </Routes>
      </Layout>
    </BrowserRouter>
  )
}

export default App

