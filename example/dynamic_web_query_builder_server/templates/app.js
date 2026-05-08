// static/app.js
// Initialize CodeMirror editors
let getEditor, postEditor, putEditor, deleteEditor, patchEditor;

// Editor initialization function
function initializeEditors() {
    // Initialize editors with initial data
    getEditor = CodeMirror.fromTextArea(document.getElementById('get-editor'), {
        mode: 'application/json',
        theme: 'material-darker',
        lineNumbers: true,
        autoCloseBrackets: true,
        matchBrackets: true,
        indentUnit: 2,
        tabSize: 2
    });

    postEditor = CodeMirror.fromTextArea(document.getElementById('post-editor'), {
        mode: 'application/json',
        theme: 'material-darker',
        lineNumbers: true,
        autoCloseBrackets: true,
        matchBrackets: true,
        indentUnit: 2,
        tabSize: 2
    });

    putEditor = CodeMirror.fromTextArea(document.getElementById('put-editor'), {
        mode: 'application/json',
        theme: 'material-darker',
        lineNumbers: true,
        autoCloseBrackets: true,
        matchBrackets: true,
        indentUnit: 2,
        tabSize: 2
    });

    patchEditor = CodeMirror.fromTextArea(document.getElementById('patch-editor'), {
        mode: 'application/json',
        theme: 'material-darker',
        lineNumbers: true,
        autoCloseBrackets: true,
        matchBrackets: true,
        indentUnit: 2,
        tabSize: 2
    });

    deleteEditor = CodeMirror.fromTextArea(document.getElementById('delete-editor'), {
        mode: 'application/json',
        theme: 'material-darker',
        lineNumbers: true,
        autoCloseBrackets: true,
        matchBrackets: true,
        indentUnit: 2,
        tabSize: 2
    });

    // Set initial values after initialization
    getEditor.setValue(`{
  "method": "GET",
  "table": "orders",
  "query": {
    "fields": [
      "orders.id",
      "orders.order_date",
      "products.name",
      "customers.city",
      "orders.quantity",
      "orders.status",
      "orders.total_amount"
    ],
    "join": [
      "JOIN products ON orders.product_id=products.id",
      "JOIN customers ON orders.customer_id=customers.id"
    ],
    "filter": ["orders.status='paid'"],
    "order_by": ["orders.order_date DESC"],
    "limit": 10
  }
}`);

    postEditor.setValue(`{
  "method": "POST",
  "table": "products",
  "data": {
    "category_id": 3,
    "name": "Desk Lamp",
    "description": "Adjustable LED desk lamp",
    "price": 45.90,
    "stock": 24,
    "created_at": "2026-04-01"
  }
}`);

    putEditor.setValue(`{
  "method": "PUT",
  "table": "products",
  "query": {
    "filter": ["id=2"]
  },
  "data": {
    "name": "Wireless Mouse Pro",
    "description": "Updated Bluetooth ergonomic mouse",
    "price": 44.50,
    "stock": 75
  }
}`);

    patchEditor.setValue(`{
  "method": "PATCH",
  "table": "orders",
  "query": {
    "filter": ["id=4"]
  },
  "data": {
    "status": "paid",
    "total_amount": 89.00
  }
}`);

    deleteEditor.setValue(`{
  "method": "DELETE",
  "table": "orders",
  "query": {
    "filter": ["id=6"]
  }
}`);
}

document.addEventListener('DOMContentLoaded', function () {
    console.log('Page loaded, HTMX version:', htmx.version);
    // Initialize editors after the DOM is fully loaded
    setTimeout(initializeEditors, 100);
});

function escapeHtml(value) {
    return value
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#039;');
}

function exportSchema() {
    const loadingElement = document.getElementById('schema-loading');
    const resultElement = document.getElementById('schema-result');

    loadingElement.style.display = 'block';
    resultElement.innerHTML = '';
    resultElement.className = 'result-container';

    fetch('/api/dynamic/schema.xml')
        .then(response => {
            return response.text().then(text => {
                if (!response.ok) {
                    throw new Error(text || `Ошибка ${response.status}: ${response.statusText}`);
                }
                return {
                    schemaPath: response.headers.get('X-Schema-Path') || 'dynamic_web_query_builder_demo.schema.xml',
                    xml: text
                };
            });
        })
        .then(({schemaPath, xml}) => {
            const preview = xml.length > 12000 ? `${xml.slice(0, 12000)}\n...` : xml;
            resultElement.innerHTML =
                `<strong>XML-схема создана:</strong> <code>${escapeHtml(schemaPath)}</code>` +
                `<pre>${escapeHtml(preview)}</pre>`;
            resultElement.className = 'result-container success';
        })
        .catch(error => {
            resultElement.innerHTML = '<strong>Ошибка:</strong> ' + escapeHtml(error.message);
            resultElement.className = 'result-container error';
        })
        .finally(() => {
            loadingElement.style.display = 'none';
        });
}

// Универсальная функция для отправки запросов
function sendRequest(method, options = {}) {
    const {
        endpoint: customEndpoint = null,
        body: requestBody = null,
        editor = null,
        validateJson = false,
        loadingElementId = null,
        resultElementId = null
    } = options;

    // Определяем endpoint
    let endpoint = customEndpoint;
    if (!endpoint && editor) {
        alert('Пожалуйста, введите endpoint');
        return;
    }

    // Определяем тело запроса
    let body = requestBody;
    if (!body && editor) {
        body = editor.getValue().trim();

        if (!body) {
            alert('Пожалуйста, введите данные для отправки');
            return;
        }

        // Валидация JSON если нужно
        if (validateJson) {
            try {
                JSON.parse(body);
            } catch (e) {
                alert('Неверный формат JSON: ' + e.message);
                return;
            }
        }
    }

    console.log(`Sending ${method} request to:`, endpoint);

    // Определяем элементы для отображения статуса
    const methodPrefix = method.toLowerCase();
    const loadingElement = document.getElementById(loadingElementId || `${methodPrefix}-loading`);
    const resultElement = document.getElementById(resultElementId || `${methodPrefix}-result`);

    if (!loadingElement || !resultElement) {
        console.error('Элементы для отображения результата не найдены');
        return;
    }

    loadingElement.style.display = 'block';
    resultElement.innerHTML = '';
    resultElement.className = 'result-container';

    const fetchOptions = {
        method: method,
        headers: {
            'Content-Type': 'application/json',
        }
    };

    if (body) {
        fetchOptions.body = body;
    }

    fetch(endpoint, fetchOptions)
        .then(response => {
            console.log('Response status:', response.status);
            loadingElement.style.display = 'none';

            // Проверяем статус ответа
            if (!response.ok) {
                // Если ошибка - пробуем получить текст ошибки
                return response.text().then(text => {
                    let errorMessage = `Ошибка ${response.status}: ${response.statusText}`;
                    try {
                        // Пытаемся распарсить как JSON если это возможно
                        const jsonError = JSON.parse(text);
                        if (jsonError.message) {
                            errorMessage = jsonError.message;
                        } else if (jsonError.error) {
                            errorMessage = jsonError.error;
                        }
                    } catch (e) {
                        // Если не JSON, используем текст
                        if (text) errorMessage = text;
                    }
                    throw new Error(errorMessage);
                });
            }

            return response.json();
        })
        .then(data => {
            resultElement.innerHTML = '<pre>' + JSON.stringify(data, null, 2) + '</pre>';
            resultElement.className = 'result-container success';
        })
        .catch(error => {
            loadingElement.style.display = 'none';
            resultElement.innerHTML = '<strong>Ошибка:</strong> ' + error.message;
            resultElement.className = 'result-container error';
        });
}
