import 'bootstrap/dist/css/bootstrap.min.css'
import 'bootstrap-icons/font/bootstrap-icons.css'
import * as bootstrap from 'bootstrap'
import './app.js'

document.querySelectorAll('[data-bs-toggle="tooltip"]').forEach(
  el => new bootstrap.Tooltip(el)
)
