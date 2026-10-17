const host = location.hostname || 'localhost'
const port = location.port === '5173' ? 8080 : (location.port || '8080')

export const API_HOST = `${host}:${port}`
export const wsUrl = (path) => `ws://${API_HOST}${path}`
