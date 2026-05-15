import { HttpClient } from '@angular/common/http'
import { Injectable } from '@angular/core'
import { Observable } from 'rxjs'

export type Breadcrumb = {
  label: string
  path: string
}

export type ProjectEntry = {
  name: string
  path: string
  kind: string
  type: 'directory' | 'file'
  description: string
  size: number
  downloadable: boolean
  navigable: boolean
}

export type ProjectStructureResponse = {
  ok: boolean
  rootLabel: string
  path: string
  breadcrumbs: Breadcrumb[]
  entries: ProjectEntry[]
  error?: string
}

@Injectable({ providedIn: 'root' })
export class ProjectStructureApiService {
  constructor(private readonly http: HttpClient) {}

  list(path: string): Observable<ProjectStructureResponse> {
    return this.http.get<ProjectStructureResponse>('/api/project/structure', {
      params: { path }
    })
  }
}
