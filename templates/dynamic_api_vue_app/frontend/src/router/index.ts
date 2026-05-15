import { createRouter, createWebHistory } from 'vue-router'
import HomeView from '../views/HomeView.vue'
import BackendAdminView from '../views/BackendAdminView.vue'

const router = createRouter({
  history: createWebHistory(),
  routes: [
    { path: '/', name: 'home', component: HomeView },
    { path: '/backend-admin', name: 'backend-admin', component: BackendAdminView }
  ]
})

export default router
