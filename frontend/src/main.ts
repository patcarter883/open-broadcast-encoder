import { createApp } from 'vue'
import { createPinia } from 'pinia'
import { Quasar, Notify, Dialog, Loading } from 'quasar'

import '@quasar/extras/material-icons/material-icons.css'
import 'quasar/src/css/index.sass'

import App from './App.vue'

const app = createApp(App)

app.use(createPinia())
app.use(Quasar, {
  plugins: { Notify, Dialog, Loading },
  config: {
    notify: {
      position: 'top-right',
      timeout: 3000,
    },
    brand: {
      primary: '#1976d2',
      secondary: '#26a69a',
      accent: '#9c27b0',
      dark: '#1d1d1d',
      'dark-page': '#121212',
      positive: '#21ba45',
      negative: '#c10015',
      info: '#31ccec',
      warning: '#f2c037',
    }
  }
})

app.mount('#app')
