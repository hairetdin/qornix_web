import { Routes } from '@angular/router'
import { HomePageComponent } from './pages/home-page.component'
import { BackendAdminPageComponent } from './pages/backend-admin-page.component'
import { ProjectStructurePageComponent } from './pages/project-structure-page.component'

export const routes: Routes = [
  { path: '', component: HomePageComponent },
  { path: 'backend-admin', component: BackendAdminPageComponent },
  { path: 'project-structure', component: ProjectStructurePageComponent },
  { path: '**', redirectTo: '' }
]
